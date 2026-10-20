# Bloodborne RPC

A lightweight, standalone **Discord Rich Presence** companion for **Bloodborne PC (bbport)**.
It shows your Current Area, Boss Detection, Hunter's level, Blood Echoes, Insight and attributes on your Discord profile while you play offline.

> **Status: work in progress.** I haven't test it on every location and every boss.

<img width="325" height="619" alt="image" src="https://github.com/user-attachments/assets/94e9df9d-defa-4528-b1c7-5f0ec05ed2f3" />


## Features

- **Current Area:** Displays your hunter's current location, such as Central Yharnam.
- **Boss Detection:** Detects boss encounters and displays the boss's name.
- **Hunter Title:** Displays your hunter's title based on level.
- **Player Stats:** Shows your character level, Blood Echoes, and Insight.
- **Character Attributes:** Displays your attributes and Max HP in the image tooltip.
- **Level-Up Notifications:** Shows level-up notifications for 2 minutes.
- **Session Progress:** Tracks levels gained during the current session.
- **Automatic Integration:** Starts and stops automatically with the game through the supported launcher.
- **Read-Only Access:** Reads game memory without modifying it.

> **Offline mode only by default.** The Hunter's Dream and Seamless Co-op have their own Rich Presence systems. When using launcher integration, the companion skips online and co-op modes.

## How it works

- **Read-only.** The tool reads the game's memory with `ReadProcessMemory` and never writes to it. No hooks, no injection.
- It attaches to `bb-probe.exe` (the process that actually runs the game; `Bloodborne.exe` is only the launcher).
- Offsets are for **Bloodborne 1.09 (CUSA03173)**, the only version bbport runs. Stats are validated before they are shown (every attribute 1–99 and `level == sum(attributes) − 50`); anything else counts as "in the menus".
- **Offline play only (default).** When started by the launcher, the tool does nothing in Seamless co-op or online modes, because those have their own presence. Set `"offline_only": false` in `config.json` to change that.

## Requirements

- Windows 10/11
- [bbport for Windows](https://github.com/Supermedo/bloodborne_pc) (or a fork of it) with companion support, see below
- Your own copy of Bloodborne 1.09. No game files are included here.
- The **Discord desktop app** running, with *User Settings → Activity Privacy → Share my detected activity* turned on

## Installation

1. Download the latest ZIP file from the [Releases](https://github.com/erezeth/bloodborne-rpc/releases) page.
2. Extract the contents into your Bloodborne PC launcher directory, where `Play Bloodborne.exe` and `run.py` are located.
3. Back up your original `run.py`.
4. Replace it with the patched `run.py` included in the release.
5. Launch Bloodborne through the launcher as usual.

Your Discord status should appear once the game is running and the companion connects successfully.

### Expected Installation Structure

After installation, the relevant files should look like this:

```text
Bloodborne PC/
├── run.py
└── discord_rpc/
    ├── BloodborneRPC.exe
    ├── companion.json
    └── config.json
```

It waits for `bb-probe.exe`, attaches, and clears the presence when the game closes. Standalone, it cannot tell the play mode, so it always runs.

## Configuration

`config.json` is optional. A missing, empty or invalid file falls back to the defaults.

| Key | Default | Meaning |
|---|---|---|
| `client_id` | built in | Discord Application ID (numbers only) |
| `process_name` | `bb-probe.exe` | Process to attach to |
| `offline_only` | `true` | Do nothing in co-op / online modes (launcher mode only) |
| `update_interval` | `15` | Seconds between presence updates (minimum 15) |
| `poll_interval` | `1.0` | Seconds between stat reads |
| `large_image` | `bloodborne_logo` | Rich Presence asset key |
| `show.titles` | `true` | Hunter title by level |
| `show.echoes` | `true` | Blood Echoes |
| `show.insight` | `true` | Insight |
| `show.level_up` | `true` | Level-up notice for 2 minutes |
| `show.session_levels` | `true` | `+N levels` gained this session |
| `show.attributes_tooltip` | `true` | Attributes and Max HP in the image tooltip |


## Troubleshooting

Everything is logged to `rpc.log` next to the tool.

| Log line | Meaning |
|---|---|
| `Companion mode: watching game pid ...` | Started by the launcher and attached to the game |
| `Presence: <top> \| <bottom>` | A presence was sent to Discord |
| `Cannot connect to Discord ...` | The Discord desktop app is not running (or runs with different privileges than the game) |
| `Image base not found yet` | The game is still loading; it retries on its own |
| `config.json is empty or invalid ...; using the defaults` | Not fatal, the defaults are used |
| `Not starting: play mode is ..., which has its own Rich Presence` | Co-op / online mode, as intended |

If your profile only shows Discord's own basic "Playing Bloodborne™" card but `rpc.log` has `Presence:` lines, check the Activity Privacy setting above. Giving your Discord application a name different from the game's may also help.

## Credits & Acknowledgements

This project wouldn't be possible without the work of the community:

- **Shiningami (GoldHEN Cheat Repository) & ASNH0** ([Bloodborne-shadps4-mod-menu](https://github.com/ASNH0/Bloodborne-shadps4-mod-menu), MIT): the original player stats pointer layout (Level, HP, Echoes, Insight, attributes).
- **CLS_AMG**: creator of [Bloodborne Seamless Co-op (BBSC)](https://github.com/Classmg22/BBSC). His open-source Discord status and mod manifest were studied as a reference for how a Bloodborne status should look and how companion programs are described.
- **Supermedo**: the [Windows port of bbport and its launcher](https://github.com/Supermedo/bloodborne_pc) that this companion plugs into.
- **Jgamrot**: the launcher fork used to develop and test the companion hook.
- **deadinside28**: [bbport](https://github.com/deadinside28/bloodborne_pc), the native Bloodborne port.
- **shadPS4 Emulator Project**: the foundation of the port's renderer; the memory layout read by this tool matches the one used under shadPS4.
- **erezeth**: Author of this repository for the C++ implementation, memory offset X-Ray debugging, and launcher integration.

## License

The patched `run.py` remains under **GPL-2.0-or-later**, matching the original launcher. The RPC companion's license is specified in its own repository. See [LICENSE](LICENSE) for the applicable license information.

*Bloodborne is a trademark of Sony Interactive Entertainment. This is an unofficial, fan-made project and is not affiliated with or endorsed by Sony Interactive Entertainment, FromSoftware, Supermedo, or Discord. No game files are included.*
