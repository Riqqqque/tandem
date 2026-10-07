# Tandem

Tandem is an OBS Studio plugin that streams the same program to Twitch, Kick and YouTube at once (or any subset), from a single encode, and merges their chats into one dock with an optional on-stream overlay.

## Features

- Extra streaming targets next to OBS's own stream: Twitch, Kick, YouTube presets, plus any RTMP/RTMPS, SRT/RIST or WHIP server.
- Shared encoder by default, so each extra platform adds almost no CPU/GPU load.
- Per-target opt-in ("go live with OBS"), live status, dropped frames, upload estimate.
- Reconnect handling that follows OBS's settings and stops endless connect-and-drop loops.
- Stream keys and API keys in Windows Credential Manager.
- Unified, read-only chat dock for Twitch, Kick and YouTube.
- Optional chat overlay for a Browser Source, local to your computer.
- No accounts, no cloud services, no telemetry.

## Supported OBS versions

| OBS | Status |
|---|---|
| 32.2.x | Supported (built against 32.2.1, tested on 32.2.2) |
| 33.0 beta | Expected to work; not tested yet |
| 32.1 and older | Not supported |

## Pages

1. [Installation](Installation.md)
2. [Quick start](Quick-Start.md)
3. [Platform setup](Platform-Setup.md): Twitch, Kick, YouTube (API key and quota)
4. [Unified chat dock](Unified-Chat-Dock.md)
5. [Chat overlay](Chat-Overlay.md)
6. [Bandwidth and performance](Bandwidth-and-Performance.md)
7. [Platform rules](Platform-Rules.md)
8. [Troubleshooting](Troubleshooting.md)
9. [FAQ](FAQ.md)
10. [Building from source](Building-from-Source.md)
11. [Credits and licenses](Credits-and-Licenses.md)
12. [Privacy and security](Privacy-and-Security.md)
