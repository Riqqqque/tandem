# Credits and licenses

Tandem is maintained by Rique and licensed under the **GNU General Public License v2.0 or later**.

| Component | Author | License | How it's used |
|---|---|---|---|
| [obs-multi-rtmp](https://github.com/sorayuki/obs-multi-rtmp) | SoraYuki | GPL-2.0-or-later | Tandem is a fork of it (upstream commit `81006873`, 0.7.4.4) |
| [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) | OBS Project | GPL-2.0-or-later | Build system and CI |
| [OBS Studio / libobs](https://github.com/obsproject/obs-studio) | OBS Project | GPL-2.0-or-later | Linked at run time, not shipped |
| [Qt 6](https://www.qt.io) | The Qt Company | LGPL-3.0 | Through OBS's copy, not shipped |
| [libcurl](https://curl.se) | Daniel Stenberg and contributors | curl (MIT-style) | HTTPS/WebSocket for chat; OBS's copy on Windows, system copy elsewhere |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | Niels Lohmann | MIT | Bundled in `dep/nlohmann-json` |

Fixes from [sorayuki/obs-multi-rtmp#594](https://github.com/sorayuki/obs-multi-rtmp/pull/594) by lauraruusula were adapted into Tandem.

Full license texts are in [LICENSE](../../LICENSE) and [CREDITS.md](../../CREDITS.md).

Tandem is not affiliated with or endorsed by Twitch, Kick, YouTube/Google or the OBS Project. Their names and trademarks belong to their owners; Tandem uses plain text labels, not logos.
