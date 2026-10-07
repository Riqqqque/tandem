# Credits

Tandem is maintained by Rique.

## Based on

**obs-multi-rtmp** by SoraYuki (雷鳴)
- https://github.com/sorayuki/obs-multi-rtmp
- License: GNU General Public License v2.0 or later (declared as `GPL-2.0-or-later` in the upstream Flatpak metadata; the upstream `LICENSE` file is the GPLv2 text, kept unchanged here)
- Forked from upstream commit `81006873b34dfe6524f671f2551ef162156a4f47` (tag 0.7.4.4, 2026-10-02)
- The multi-output engine, the target editor and the property widgets come from obs-multi-rtmp. Upstream contributors are listed in the upstream git history, which this repository keeps.

Fixes ported from upstream pull requests:
- [sorayuki/obs-multi-rtmp#594](https://github.com/sorayuki/obs-multi-rtmp/pull/594) by lauraruusula: UI-thread safety, the edit dialog layout double-parenting, property-widget fixes and leak fixes (adapted; the encoder reference-count changes were reworked).

Ideas taken from discussions in the obs-multi-rtmp issue tracker are listed in [CHANGELOG.md](CHANGELOG.md).

## Built with

**obs-plugintemplate** by the OBS Project
- https://github.com/obsproject/obs-plugintemplate
- License: GNU General Public License v2.0 or later
- Build fixes ported from the template's open pull requests and issues (Windows install path, missing macOS build number, the Ubuntu `--debug` packaging flag).

**OBS Studio / libobs / obs-frontend-api** by the OBS Project
- https://github.com/obsproject/obs-studio
- License: GNU General Public License v2.0 or later
- Tandem links against libobs and the frontend API at run time; it does not ship them.

**Qt 6** by The Qt Company and contributors
- https://www.qt.io
- License: GNU Lesser General Public License v3 (as distributed with OBS Studio)
- Used through the copy that ships with OBS; Tandem does not ship Qt.

**libcurl** by Daniel Stenberg and contributors
- https://curl.se
- License: curl license (MIT/X derivative), text below
- Used for HTTPS and WebSocket chat connections. On Windows Tandem uses the libcurl that ships with OBS; elsewhere it uses the system libcurl. Tandem does not ship libcurl.

## Bundled third-party code

**nlohmann/json 3.12.0** by Niels Lohmann
- https://github.com/nlohmann/json
- License: MIT, text below
- File: `dep/nlohmann-json/json.hpp` (SHA-256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`, identical to the 3.12.0 release asset)

No GPLv3-only code is included. The chat providers, the chat dock, the overlay server and page are original to Tandem.

## Trademarks

Tandem is not affiliated with, endorsed by or sponsored by Twitch Interactive, Kick Streaming, Google/YouTube or the OBS Project. Twitch, Kick, YouTube and OBS are trademarks of their respective owners. Tandem shows platform names as plain text labels and does not use platform logos.

---

## Third-party license texts

### nlohmann/json (MIT)

```
MIT License

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### libcurl (curl license)

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2025, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder.
```

### GPL-2.0 (obs-multi-rtmp, obs-plugintemplate, OBS Studio, Tandem)

See [LICENSE](LICENSE).

### LGPL-3.0 (Qt)

Qt is used through OBS Studio's own copy. Its license is available at https://www.gnu.org/licenses/lgpl-3.0.html and in your OBS installation.
