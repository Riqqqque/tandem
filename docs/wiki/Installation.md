# Installation

Close OBS before installing or updating.

## Download and verify

Download from the [Releases](https://github.com/Riqqqque/tandem/releases) page. Every release lists the SHA-256 checksum of each file. Check yours before running it:

- Windows (PowerShell): `Get-FileHash .\tandem-0.1.0-windows-x64-Installer.exe -Algorithm SHA256`
- macOS: `shasum -a 256 tandem-0.1.0-macos-universal.pkg`
- Linux: `sha256sum tandem-0.1.0-x86_64-linux-gnu.deb`

The hash must match the one on the release page exactly. Release files are built by GitHub Actions from the tagged source.

## Windows

**Installer:** run `tandem-<version>-windows-x64-Installer.exe`. It installs into `C:\ProgramData\obs-studio\plugins\tandem` (for all users, no admin needed). Running it again offers to update or remove Tandem; removal only deletes Tandem's own folder.

**Zip:** extract `tandem-<version>-windows-x64.zip` into your OBS folder (usually `C:\Program Files\obs-studio`) so that `obs-plugins\64bit\tandem.dll` and `data\obs-plugins\tandem\` land next to OBS's own plugins.

Windows SmartScreen or an antivirus may warn about a new, unsigned plugin. See [Troubleshooting](Troubleshooting.md#antivirus-warnings).

## macOS

Run the `.pkg`. It installs into `~/Library/Application Support/obs-studio/plugins`. On first start macOS may ask you to allow the plugin in System Settings → Privacy & Security.

Current status: the macOS build is produced by CI but has not been tested on a Mac yet. Stream keys are stored in the OBS profile folder on macOS (Keychain support is planned).

## Linux

- **Ubuntu / Debian:** install the `.deb` with `sudo apt install ./tandem-<version>-x86_64-linux-gnu.deb`.
- **Flatpak OBS:** install the `.flatpak` bundle with `flatpak install --user ./tandem.flatpak`.

Current status: Linux builds are produced by CI but have not been tested on a desktop yet. Twitch and Kick chat need a libcurl built with WebSocket support (curl 8.11 or newer enables it by default; some distributions build older versions without it). If yours lacks it, the chat dock says so; streaming and YouTube chat are not affected. Stream keys are stored in the OBS profile folder on Linux.

## Coming from obs-multi-rtmp

Tandem reads `obs-multi-rtmp.json` from each OBS profile the first time it starts and imports your targets into `tandem.json` (keys move to Credential Manager on Windows). The old file is left as it was. Uninstall obs-multi-rtmp afterwards so both plugins don't run at once.

## Uninstall

- Windows: run the installer again and choose **No = remove**, or delete `C:\ProgramData\obs-studio\plugins\tandem`.
- Settings stay in each OBS profile as `tandem.json`; stored keys stay in Credential Manager under `Tandem/`. See [Privacy and security](Privacy-and-Security.md) to remove them.
