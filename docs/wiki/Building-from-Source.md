# Building from source

Tandem uses the OBS Project's plugin template (CMake presets and GitHub Actions scripts).

## Windows

Requirements: Visual Studio 2022 (or Build Tools) with "Desktop development with C++" and the Windows 10/11 SDK, CMake 3.28 or newer, Git.

```
git clone https://github.com/Riqqqque/tandem.git
cd tandem
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

The first configure downloads and builds the OBS sources, obs-deps and Qt listed in `buildspec.json` into `.deps/` (a few minutes). The plugin ends up in `build_x64/rundir/RelWithDebInfo/`:

- `tandem.dll` → copy to `C:\ProgramData\obs-studio\plugins\tandem\bin\64bit\`
- `tandem/` (locale files) → copy its contents to `C:\ProgramData\obs-studio\plugins\tandem\data\`

To test without touching your normal OBS setup, use a portable copy of OBS: copy the OBS folder somewhere, create an empty `portable_mode.txt` next to its `bin` folder, and put the plugin in that copy's `obs-plugins\64bit` and `data\obs-plugins\tandem`. Portable OBS keeps its own settings and doesn't load plugins from ProgramData.

## macOS

Requirements: Xcode 16+, CMake 3.28+.

```
cmake --preset macos
cmake --build --preset macos --config RelWithDebInfo
```

## Ubuntu

Requirements: see `.github/scripts/.Aptfile` (OBS development packages, Qt 6, CMake, Ninja, libcurl).

```
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
```

## Flatpak

`flatpak/build.sh` builds a Flatpak extension bundle (`com.obsproject.Studio.Plugin.Tandem`) for the Flathub OBS Studio with `flatpak-builder`. The result is `release/tandem.flatpak`.

## Unit tests

The chat providers and the overlay server don't depend on OBS or Qt:

```
cmake -S tests -B build_tests
cmake --build build_tests --config RelWithDebInfo
ctest --test-dir build_tests -C RelWithDebInfo --output-on-failure
```

On Windows, point CMake at a libcurl, e.g. the one from the OBS deps: `-DCMAKE_PREFIX_PATH=<repo>/.deps/obs-deps-2026-07-15-x64`.

`tandem-chat-probe` connects to a live chat and prints events, which helps when a platform changes something:

```
tandem-chat-probe twitch <channel> 30
tandem-chat-probe kick <slug> 30
TANDEM_YT_KEY=<key> tandem-chat-probe youtube <video> 30
```

## Releases

Pushing a tag such as `0.1.0` runs the **Push** workflow: it builds Windows, macOS, Ubuntu and Flatpak packages and creates a draft GitHub release with SHA-256 checksums of every file. Review the draft, then publish it.

## Source layout

| Path | Contents |
|---|---|
| `src/tandem-dock.cpp` | Module entry point, outputs dock |
| `src/push-widget.cpp` | One streaming target: output lifecycle, status, reconnect guard |
| `src/edit-widget.cpp` | Target settings dialog |
| `src/output-config.*` | `tandem.json` load/save, credential migration |
| `src/secrets.*` | Windows Credential Manager wrapper |
| `src/platforms.h` | Platform presets |
| `src/chat-controller.*`, `src/chat-dock.cpp`, `src/settings-dialog.cpp` | Chat lifecycle, dock and settings UI |
| `src/chat/` | Chat providers, parsers, networking (libcurl), event hub |
| `src/overlay/` | Local overlay HTTP/SSE server and page |
| `tests/` | Unit tests, fixtures, chat probe |
