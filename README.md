# Discord Status for OBS

[![Build](https://github.com/bogenpirat/obs-discord-status/actions/workflows/build.yaml/badge.svg)](https://github.com/bogenpirat/obs-discord-status/actions/workflows/build.yaml)
[![OBS Studio](https://img.shields.io/badge/OBS%20Studio-31%2B-302e31?logo=obsstudio)](https://obsproject.com/)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078d4)](#installation)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue)](LICENSE)

An OBS Studio plugin that watches your **local Discord client** and reacts to
voice status changes with configurable OBS actions — hide your webcam while
you're muted, duck game audio while a teammate talks, switch an overlay on
when you join a call, and anything in between.

No bot, no browser source, no OBS websocket scripts: the plugin talks directly
to the Discord desktop app over its local RPC interface and drives OBS natively.

## ✨ Features

**Triggers** — react to any of these Discord voice events:

| Category | Events |
|---|---|
| Voice channel | you joined · you left |
| Your state | muted · unmuted · deafened · undeafened |
| Participants | someone joined · someone left *(optionally a specific user)* |
| Speaking | a user started/stopped speaking *(optionally a specific user)* · anyone speaking · everyone silent |

**Actions** — every trigger can run any number of them (1:n):

- 👁️ **Show / hide a source** — in a specific scene or across all scenes
- 🔇 **Mute / unmute an audio source**
- 🎚️ **Set the volume** of an audio source (0–200 %)

Rules are managed in a built-in dialog under **Tools → Discord Status Settings**,
live-applied and persisted across restarts.

**Example rules:**

- *Muted myself* → hide `Webcam`, show `BRB overlay`
- *Anyone started speaking* → set `Game Audio` volume to 40 %
- *Everyone stopped speaking* → set `Game Audio` volume to 100 %
- *TeammateXY joined the channel* → unmute `Discord Audio` capture

## 📦 Installation

1. Grab the latest zip from [Releases](https://github.com/bogenpirat/obs-discord-status/releases)
   (or a build artifact from the Actions tab).
2. Extract it into your OBS installation directory (default:
   `C:\Program Files\obs-studio`) — or copy the plugin into
   `%ProgramData%\obs-studio\plugins\obs-discordstatus\` if you prefer keeping
   your install directory clean.
3. Restart OBS. The plugin appears under **Tools → Discord Status Settings**.

### Discord authorization

On first connect, Discord pops an authorization prompt — click **Authorize**
once and you're set (the token is cached). Two auth modes are available in the
settings dialog:

| Mode | Setup | Notes |
|---|---|---|
| **StreamKit** *(default)* | none | Authorizes via Discord's own StreamKit app over the local WebSocket. Unofficial but stable for years — the same mechanism StreamKit overlays and Discord Reactive Images use. |
| **Own application** | create a free app in the [Discord Developer Portal](https://discord.com/developers/applications), paste its Client ID + Secret | Fully within Discord's API rules; uses the local IPC pipe. Your safety net if the StreamKit endpoint ever changes. |

> **Privacy note:** the plugin only talks to your local Discord client (plus a
> single HTTPS request to exchange the OAuth code). Nothing is sent anywhere
> else; the OAuth token and rules live in
> `%APPDATA%\obs-studio\plugin_config\obs-discordstatus\config.json`.

## 🔨 Building

Based on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate);
dependencies (libobs, Qt6, obs-deps) are fetched automatically during configure.

**Requirements:** Visual Studio 2022 (or Build Tools) with C++ workload, CMake 3.28+, Windows 10 SDK ≥ 10.0.20348.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The built plugin lands in `build_x64\RelWithDebInfo` (and a ready-to-copy
layout in `build_x64\rundir`).

<details>
<summary>Alternative: Ninja dev build (no full VS install)</summary>

If you only have Build Tools with a pinned MSVC toolset, use the helper script,
which drives the `windows-dev` preset from `CMakeUserPresets.json` inside a
`vcvarsall` environment:

```powershell
pwsh scripts/build.ps1            # build
pwsh scripts/build.ps1 -Deploy    # build + install into %ProgramData%
pwsh scripts/build.ps1 -Package   # build + create release zip
```
</details>

## ⚙️ How it works

The plugin connects to the Discord desktop client's local RPC server
(`127.0.0.1:6463-6472` WebSocket, or the `discord-ipc` named pipe), subscribes
to `VOICE_CHANNEL_SELECT`, `VOICE_SETTINGS_UPDATE`, `VOICE_STATE_*` and
`SPEAKING_*` events, and normalizes them into edge-triggered signals. A small
rules engine matches those edges against your configured rules and executes the
actions through libobs / the OBS frontend API on the UI thread.

## License

GPL-2.0 — see [LICENSE](LICENSE).
