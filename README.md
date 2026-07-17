# Discord Status for OBS

An OBS Studio plugin that watches your **local Discord client** over its RPC
interface and reacts to voice status changes with configurable OBS actions.

## Triggers

- Joined / left a voice channel
- Muted / unmuted yourself, deafened / undeafened yourself
- Someone joined / left your voice channel (optionally a specific user)
- A user started / stopped speaking (optionally a specific user)
- Anyone started speaking / everyone stopped speaking

## Actions (each trigger can run any number of them)

- Show / hide a source (in one scene or in all scenes)
- Mute / unmute an audio source
- Set the volume of an audio source

Configure rules under **Tools → Discord Status Settings** in OBS.

## Installation (Windows)

Extract the release zip into your OBS installation directory
(`C:\Program Files\obs-studio`), or copy
`bin\64bit` + `data` into `%ProgramData%\obs-studio\plugins\obs-discordstatus\`.

## Discord authorization

Two modes, selectable in the settings dialog:

- **StreamKit (default, no setup):** authorizes through Discord's own StreamKit
  application. First connect pops an authorization prompt inside Discord —
  click *Authorize* once. Unofficial but stable for years (used by StreamKit
  overlays and Discord Reactive Images).
- **Own Discord application:** create an application at
  https://discord.com/developers/applications, paste its *Client ID* and
  *Client Secret* into the plugin settings. Fully within Discord's rules;
  works even if the StreamKit endpoint ever goes away. Uses the local IPC
  pipe transport.

The OAuth token is cached in
`%APPDATA%\obs-studio\plugin_config\obs-discordstatus\config.json` so the
prompt appears only once.

## Building

- CI / release builds: standard [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
  presets (`cmake --preset windows-x64`, VS 2022).
- Local dev build (Ninja + vcvars): `pwsh scripts/build.ps1 [-Deploy] [-Package]`.
  See `CMakeUserPresets.json` — needed when only VS 2026 Build Tools with the
  14.44 toolset are available.
