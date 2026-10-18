// SPDX-FileCopyrightText: Copyright 2026 [NamaKamu]
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bloodborne PC (bbport) - Discord Rich Presence companion.
// READ-ONLY: this tool never writes to the game's memory.
// 
// Based on the original Python implementation by CLS_AMG (BBSC).
// Pointer layout credit: GoldHEN Cheat Repository (Shiningami) and
// ASNH0/Bloodborne-shadps4-mod-menu (MIT).

#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using clk = std::chrono::steady_clock;

// ------------------------------------------------------------------ constants

constexpr uintptr_t GAME_DATA_MAN = 0x553B130;
constexpr int PGD_HP = 0x14, PGD_MAXHP = 0x18;
constexpr int PGD_INSIGHT = 0x84, PGD_LEVEL = 0x90, PGD_ECHOES = 0x94;
constexpr int PGD_READ_SIZE = 0x98;

struct AttrDef { const char* name; int off; };
constexpr AttrDef PGD_ATTR_DEFS[6] = {
    {"VIT", 0x40}, {"END", 0x48}, {"STR", 0x58},
    {"SKL", 0x60}, {"BLT", 0x68}, {"ARC", 0x70},
};

struct TitleDef { int level; const char* name; };
constexpr TitleDef HUNTER_TITLES[] = {
    {1, "Fledgling Hunter"}, {15, "Blooded Hunter"},
    {40, "Seasoned Hunter"}, {80, "Veteran Hunter"},
    {120, "Dreaming Hunter"}, {180, "Hunter of Hunters"},
};

// ------------------------------------------------------------------ globals

static fs::path g_exe_dir;

// ------------------------------------------------------------------ utils

static std::string hex(uintptr_t v) {
    char b[32]; std::snprintf(b, sizeof(b), "0x%llx", (unsigned long long)v);
    return b;
}

static void log(const std::string& msg) {
    char ts[16];
    std::time_t t = std::time(nullptr);
    std::tm tb{}; localtime_s(&tb, &t);
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tb);

    std::string line = std::string("[") + ts + "] " + msg;

    std::ofstream f(g_exe_dir / "rpc.log", std::ios::app);
    if (f) f << line << "\n";

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        std::string console_line = line + "\n";
        WriteFile(out, console_line.data(), (DWORD)console_line.size(), &written, nullptr);
    }
}

static void fatal(const std::string& msg) {
    log("ERROR: " + msg);
    if (!std::getenv("BB_GAME_PID")) {
        MessageBoxA(nullptr, msg.c_str(), "BloodborneRPC", MB_ICONERROR | MB_OK);
    }
}

static fs::path exe_directory() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return fs::current_path();
    try { return fs::path(std::wstring(buf, n)).parent_path(); }
    catch (...) { return fs::current_path(); }
}

// ------------------------------------------------------------------ config

struct Config {
    std::string client_id;
    std::string process_name = "bb-probe.exe";
    std::vector<uintptr_t> image_bases = {0x800000000};
    int attach_delay = 5;
    int update_interval = 15;
    double poll_interval = 1.0;
    std::string large_image = "bloodborne_logo";
    std::string small_image;
    json buttons = json::array();
    bool offline_only = true;
    bool show_titles = true;
    bool show_echoes = true;
    bool show_insight = true;
    bool show_level_up = true;
    bool show_session_levels = true;
    bool show_attributes_tooltip = true;
};

static Config load_config(const fs::path& path) {
    Config c;
    if (!fs::exists(path)) return c;
    try {
        std::ifstream f(path);
        json j; f >> j;
        if (!j.is_object()) throw std::runtime_error("not an object");

        if (j.contains("client_id") && j["client_id"].is_string()) c.client_id = j["client_id"].get<std::string>();
        if (j.contains("process_name") && j["process_name"].is_string()) c.process_name = j["process_name"].get<std::string>();
        if (j.contains("image_base_candidates") && j["image_base_candidates"].is_array()) {
            std::vector<uintptr_t> bases;
            for (const auto& v : j["image_base_candidates"]) {
                if (v.is_string()) {
                    uintptr_t addr = std::strtoull(v.get<std::string>().c_str(), nullptr, 0);
                    if (addr) bases.push_back(addr);
                } else if (v.is_number_unsigned()) {
                    bases.push_back(v.get<uintptr_t>());
                }
            }
            if (!bases.empty()) c.image_bases = std::move(bases);
        }
        if (j.contains("attach_delay")) c.attach_delay = j["attach_delay"].get<int>();
        if (j.contains("update_interval")) c.update_interval = j["update_interval"].get<int>();
        if (j.contains("poll_interval")) c.poll_interval = j["poll_interval"].get<double>();
        if (j.contains("large_image")) c.large_image = j["large_image"].get<std::string>();
        if (j.contains("small_image")) c.small_image = j["small_image"].get<std::string>();
        if (j.contains("buttons")) c.buttons = j["buttons"];
        if (j.contains("offline_only")) c.offline_only = j["offline_only"].get<bool>();
        
        if (j.contains("show") && j["show"].is_object()) {
            const auto& s = j["show"];
            if (s.contains("titles")) c.show_titles = s["titles"].get<bool>();
            if (s.contains("echoes")) c.show_echoes = s["echoes"].get<bool>();
            if (s.contains("insight")) c.show_insight = s["insight"].get<bool>();
            if (s.contains("level_up")) c.show_level_up = s["level_up"].get<bool>();
            if (s.contains("session_levels")) c.show_session_levels = s["session_levels"].get<bool>();
            if (s.contains("attributes_tooltip")) c.show_attributes_tooltip = s["attributes_tooltip"].get<bool>();
        }
    } catch (...) {}
    return c;
}

// ------------------------------------------------------------------ process

class Process {
    HANDLE h_ = nullptr;
public:
    Process() = default;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    Process& operator=(Process&& o) noexcept {
        if (this != &o) { close(); h_ = o.h_; o.h_ = nullptr; }
        return *this;
    }
    ~Process() { close(); }

    bool open(DWORD pid) {
        close();
        h_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        return h_ != nullptr;
    }
    void close() {
        if (h_) { CloseHandle(h_); h_ = nullptr; }
    }
    bool read(uintptr_t addr, void* buf, size_t size) const {
        if (!h_) return false;
        SIZE_T got = 0;
        return ReadProcessMemory(h_, (LPCVOID)addr, buf, size, &got) && got == size;
    }
    template <typename T>
    bool read(uintptr_t addr, T& out) const {
        return read(addr, &out, sizeof(T));
    }
};

static DWORD find_process(const std::string& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, std::wstring(name.begin(), name.end()).c_str()) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool pid_alive(DWORD pid, const std::string& name_hint = "") {
    if (!pid) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD exit_code = 0;
    bool alive = GetExitCodeProcess(h, &exit_code) && exit_code == STILL_ACTIVE;
    if (alive && !name_hint.empty()) {
        wchar_t buf[MAX_PATH]; DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
            fs::path p(std::wstring(buf, size));
            if (_wcsicmp(p.filename().c_str(), std::wstring(name_hint.begin(), name_hint.end()).c_str()) != 0) {
                alive = false;
            }
        }
    }
    CloseHandle(h);
    return alive;
}

static uintptr_t find_base(const Process& p, const std::vector<uintptr_t>& bases) {
    for (auto b : bases) {
        uint8_t tmp[8];
        if (p.read(b + GAME_DATA_MAN, tmp, sizeof(tmp))) return b;
    }
    return 0;
}

// ------------------------------------------------------------------ stats

struct Stats {
    int level, insight, echoes, hp, maxhp;
    std::array<int, 6> attrs;
};

static std::optional<Stats> read_stats(const Process& p, uintptr_t base) {
    uint64_t gdm = 0;
    if (!p.read(base + GAME_DATA_MAN, gdm) || !gdm) return std::nullopt;
    uint64_t pgd = 0;
    if (!p.read(gdm + 8, pgd) || !pgd) return std::nullopt;

    uint8_t blk[PGD_READ_SIZE];
    if (!p.read(pgd, blk, sizeof(blk))) return std::nullopt;

    auto i32 = [&](int off) {
        int32_t v; std::memcpy(&v, blk + off, 4); return v;
    };

    Stats s{};
    for (int i = 0; i < 6; ++i) s.attrs[i] = i32(PGD_ATTR_DEFS[i].off);
    for (int a : s.attrs) if (a < 1 || a > 99) return std::nullopt;

    s.level = i32(PGD_LEVEL);
    int sum = 0; for (int a : s.attrs) sum += a;
    if (s.level != sum - 50) return std::nullopt;

    s.insight = i32(PGD_INSIGHT);
    s.echoes  = i32(PGD_ECHOES);
    s.hp      = i32(PGD_HP);
    s.maxhp   = i32(PGD_MAXHP);

    if (s.insight < 0 || s.insight > 999) return std::nullopt;
    if (s.echoes  < 0 || s.echoes  > 999999999) return std::nullopt;
    if (s.maxhp   <= 0 || s.maxhp  >= 100000) return std::nullopt;
    if (s.hp      < 0 || s.hp      > s.maxhp) return std::nullopt;
    return s;
}

// ------------------------------------------------------------------ session

class Session {
    static constexpr int LEVEL_UP_SECONDS = 120;
    int last_level_ = -1;
    int known_level_ = -1;
    int levels_gained_ = 0;
    int level_up_to_ = 0;
    clk::time_point level_up_until_{};

public:
    void update(const std::optional<Stats>& stats, clk::time_point now) {
        if (!stats) { last_level_ = -1; return; }
        int level = stats->level;
        if (last_level_ < 0 && known_level_ >= 0) {
            int diff = level - known_level_;
            if (diff < 0 || diff > 5) {
                levels_gained_ = 0;
                level_up_until_ = {};
            }
        }
        known_level_ = level;
        if (last_level_ >= 0 && level > last_level_ && level - last_level_ <= 5) {
            levels_gained_ += level - last_level_;
            level_up_to_ = level;
            level_up_until_ = now + std::chrono::seconds(LEVEL_UP_SECONDS);
        }
        last_level_ = level;
    }

    int levels_gained() const { return levels_gained_; }
    bool level_up_active(clk::time_point now) const { return now < level_up_until_; }
    int level_up_to() const { return level_up_to_; }
};

// ------------------------------------------------------------------ discord pipe

class DiscordPipe {
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
public:
    ~DiscordPipe() { close(); }
    void close() {
        if (pipe_ != INVALID_HANDLE_VALUE) { CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; }
    }
    bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    bool connect_pipe(const std::string& client_id) {
        close();
        for (int i = 0; i < 10; ++i) {
            std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) break;
        }
        if (pipe_ == INVALID_HANDLE_VALUE) return false;

        std::string hs = std::string("{\"v\":1,\"client_id\":\"") + client_id + "\"}";
        if (!send(0, hs)) { close(); return false; }
        return drain(true);
    }

    bool send(uint32_t opcode, const std::string& payload) {
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        std::string frame(8, '\0');
        std::memcpy(&frame[0], &opcode, 4);
        uint32_t len = (uint32_t)payload.size();
        std::memcpy(&frame[4], &len, 4);
        frame += payload;
        DWORD written = 0;
        return WriteFile(pipe_, frame.data(), (DWORD)frame.size(), &written, nullptr) && written == frame.size();
    }

    bool drain(bool wait) {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) { close(); return false; }
            if (avail < 8 && !wait) return true;

            uint32_t hdr[2] = {0, 0};
            DWORD got = 0;
            if (!ReadFile(pipe_, hdr, 8, &got, nullptr) || got != 8) { close(); return false; }
            std::string body(hdr[1], '\0');
            if (hdr[1] > 0) {
                if (!ReadFile(pipe_, body.data(), hdr[1], &got, nullptr) || got != hdr[1]) { close(); return false; }
            }
            if (hdr[0] == 2) {
                log("Discord closed the connection: " + body);
                close(); return false;
            }
            wait = false;
        }
    }
};

// ------------------------------------------------------------------ presence

static std::string format_thousands(int n) {
    std::string s = std::to_string(n);
    std::string out;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        if (c > 0 && c % 3 == 0) out = ',' + out;
        out = s[i] + out;
        ++c;
    }
    return out;
}

static json build_presence(const std::optional<Stats>& stats, Session& session, const Config& cfg, int64_t session_start_s, clk::time_point now) {
    json data;
    data["large_image"] = cfg.large_image;
    data["start"] = session_start_s;
    if (!cfg.small_image.empty()) data["small_image"] = cfg.small_image;
    if (cfg.buttons.is_array() && !cfg.buttons.empty()) {
        json btns = json::array();
        int n = 0;
        for (const auto& b : cfg.buttons) { if (n++ >= 2) break; btns.push_back(b); }
        data["buttons"] = std::move(btns);
    }

    if (!stats) {
        data["details"] = "Wandering the menus";
        data["state"] = "Waiting for a hunter...";
        data["large_text"] = "Bloodborne PC";
        return data;
    }

    const int level = stats->level;
    std::string title = "Hunter";
    if (cfg.show_titles) {
        title = HUNTER_TITLES[0].name;
        for (const auto& t : HUNTER_TITLES) if (level >= t.level) title = t.name;
    }

    if (cfg.show_level_up && session.level_up_active(now)) {
        char b[64]; std::snprintf(b, sizeof(b), "\u2726 Ascended to Level %d", session.level_up_to());
        data["details"] = b;
    } else {
        char b[128]; std::snprintf(b, sizeof(b), "%s \u2022 Lv %d", title.c_str(), level);
        data["details"] = b;
    }

    std::vector<std::string> parts;
    if (cfg.show_echoes) {
        char b[128]; std::snprintf(b, sizeof(b), "\U0001FA78 %s Echoes", format_thousands(stats->echoes).c_str());
        parts.push_back(b);
    }
    if (cfg.show_insight) {
        char b[64]; std::snprintf(b, sizeof(b), "Insight %d", stats->insight);
        parts.push_back(b);
    }
    if (cfg.show_session_levels && session.levels_gained() > 0) {
        int g = session.levels_gained();
        char b[64]; std::snprintf(b, sizeof(b), "+%d level%s", g, g == 1 ? "" : "s");
        parts.push_back(b);
    }
    if (parts.empty()) {
        data["state"] = "Hunting in Yharnam";
    } else {
        std::string s;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) s += " \u2022 ";
            s += parts[i];
        }
        data["state"] = s;
    }

    if (cfg.show_attributes_tooltip) {
        std::string attrs;
        for (int i = 0; i < 6; ++i) {
            if (i) attrs += " ";
            attrs += PGD_ATTR_DEFS[i].name;
            attrs += " ";
            attrs += std::to_string(stats->attrs[i]);
        }
        char b[256]; std::snprintf(b, sizeof(b), "%s \u2022 Max HP %d", attrs.c_str(), stats->maxhp);
        data["large_text"] = b;
    } else {
        data["large_text"] = "Bloodborne PC";
    }

    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it->is_string() && it->get<std::string>().size() > 128) {
            *it = it->get<std::string>().substr(0, 128);
        }
    }
    return data;
}

static json to_discord_activity(const json& flat) {
    json a = json::object();
    if (flat.contains("details")) a["details"] = flat["details"];
    if (flat.contains("state"))   a["state"]   = flat["state"];
    if (flat.contains("start"))   a["timestamps"] = {{"start", (int64_t)flat["start"] * 1000}};

    json assets = json::object();
    if (flat.contains("large_image")) assets["large_image"] = flat["large_image"];
    if (flat.contains("large_text"))  assets["large_text"]  = flat["large_text"];
    if (flat.contains("small_image")) assets["small_image"] = flat["small_image"];
    if (flat.contains("small_text"))  assets["small_text"]  = flat["small_text"];
    if (!assets.empty()) a["assets"] = std::move(assets);

    if (flat.contains("buttons") && flat["buttons"].is_array() && !flat["buttons"].empty()) a["buttons"] = flat["buttons"];
    return a;
}

// ------------------------------------------------------------------ play mode

static std::string play_mode() {
    const char* pid = std::getenv("BB_GAME_PID");
    if (!pid || !*pid) return ""; 
    for (const char* c = pid; *c; ++c) if (!std::isdigit((unsigned char)*c)) return "";
    if (auto coop = std::getenv("BB_COOP"); coop && std::string(coop) == "1") return "coop";
    if (auto online = std::getenv("BB_ONLINE"); online && std::string(online) == "1") {
        auto backend = std::getenv("BB_NET_BACKEND");
        return (backend && std::string(backend) == "thd") ? "thd" : "online";
    }
    return "offline";
}

static bool should_skip(const Config& cfg, const std::string& mode, std::string& reason) {
    if (cfg.offline_only && !mode.empty() && mode != "offline") {
        reason = "play mode is " + mode + ", which has its own Rich Presence";
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ entry & run

static void probe_mode(const Config& cfg) {
    DWORD pid = find_process(cfg.process_name);
    if (!pid) { log(cfg.process_name + " is not running"); return; }
    Process p;
    if (!p.open(pid)) { log("cannot open process"); return; }
    uintptr_t base = find_base(p, cfg.image_bases);
    log("pid=" + std::to_string(pid) + " base=" + (base ? hex(base) : "NO"));
    if (!base) return;
    auto stats = read_stats(p, base);
    if (!stats) { log("stats: null (title screen / not loaded)"); return; }
    log("level=" + std::to_string(stats->level) + " echoes=" + std::to_string(stats->echoes)
        + " insight=" + std::to_string(stats->insight) + " maxhp=" + std::to_string(stats->maxhp));
}

static void run(const Config& cfg) {
    if (cfg.client_id.empty() || !std::all_of(cfg.client_id.begin(), cfg.client_id.end(), ::isdigit)) {
        fatal("config.json: client_id must be your Discord Application ID (numbers only).");
        return;
    }

    const bool companion = [] {
        const char* pid = std::getenv("BB_GAME_PID");
        return pid && *pid && std::all_of(pid, pid + std::strlen(pid), ::isdigit);
    }();
    const DWORD companion_pid = companion ? (DWORD)std::strtoul(std::getenv("BB_GAME_PID"), nullptr, 10) : 0;

    const std::string mode = play_mode();
    std::string reason;
    if (should_skip(cfg, mode, reason)) {
        log("Not starting: " + reason + ".");
        return;
    }
    if (companion) log("Companion mode: watching game pid " + std::to_string(companion_pid));
    else            log("Waiting for " + cfg.process_name + " ...");

    const auto poll = std::chrono::milliseconds((int)(std::max(0.25, std::min(5.0, cfg.poll_interval)) * 1000));
    const int update_interval = std::max(15, cfg.update_interval);
    const std::string check_name = companion ? "" : cfg.process_name;

    while (true) {
        DWORD pid = companion ? companion_pid : find_process(cfg.process_name);
        if (companion && !pid_alive(pid)) { log("Game process is not running; exiting."); return; }
        if (!pid) { std::this_thread::sleep_for(std::chrono::seconds(3)); continue; }

        std::this_thread::sleep_for(std::chrono::seconds(cfg.attach_delay));
        if (companion && !pid_alive(pid)) { log("Game closed before the tool attached; exiting."); return; }

        Process proc;
        if (!proc.open(pid)) { log("Cannot open process; retrying..."); std::this_thread::sleep_for(std::chrono::seconds(10)); continue; }
        uintptr_t base = find_base(proc, cfg.image_bases);
        if (!base) { log("Image base not found yet. Retrying..."); std::this_thread::sleep_for(std::chrono::seconds(5)); continue; }

        DiscordPipe discord;
        while (pid_alive(pid, check_name)) {
            if (discord.connect_pipe(cfg.client_id)) break;
            log("Cannot connect to Discord. Retrying...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        log("Attached to pid " + std::to_string(pid) + ", base " + hex(base));

        const int64_t start_s = (int64_t)std::time(nullptr);
        Session session;
        json last_payload;
        auto last_sent = clk::time_point{};

        while (pid_alive(pid, check_name)) {
            if (!discord.connected()) {
                if (!discord.connect_pipe(cfg.client_id)) { std::this_thread::sleep_for(std::chrono::seconds(5)); continue; }
            }
            discord.drain(false);
            if (!discord.connected()) continue;

            auto stats = read_stats(proc, base);
            session.update(stats, clk::now());
            json payload = build_presence(stats, session, cfg, start_s, clk::now());

            auto now = clk::now();
            if (payload != last_payload && (now - last_sent) >= std::chrono::seconds(update_interval)) {
                json activity = to_discord_activity(payload);
                json cmd = {
                    {"cmd", "SET_ACTIVITY"},
                    {"args", {{"pid", (int)pid}, {"activity", activity}}},
                    {"nonce", std::to_string((uint64_t)now.time_since_epoch().count())}
                };
                if (discord.send(1, cmd.dump())) {
                    last_payload = payload;
                    last_sent = now;
                    log("Presence: " + payload.value("details", std::string()) + " | " + payload.value("state", std::string()));
                } else {
                    discord.close();
                }
            }
            std::this_thread::sleep_for(poll);
        }
        if (discord.connected()) {
            json cmd = {{"cmd", "SET_ACTIVITY"}, {"args", {{"pid", (int)pid}}}, {"nonce", "0"}};
            discord.send(1, cmd.dump());
        }
        log("Game closed.");
        if (companion) return;
        log("Waiting again...");
    }
}

static bool has_flag(const wchar_t* flag) {
    return wcsstr(GetCommandLineW(), flag) != nullptr;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    g_exe_dir = exe_directory();
    const Config cfg = load_config(g_exe_dir / "config.json");

    if (has_flag(L"--probe")) {
        probe_mode(cfg);
        return 0;
    }
    run(cfg);
    return 0;
}