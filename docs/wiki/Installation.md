# Installation

## Windows (the easy way)

1. [**Download the installer**](https://github.com/Riqqqque/tandem/releases/latest/download/tandem-windows-installer.exe) (always the newest version).
2. Double-click it. If Windows shows "Windows protected your PC", click **More info** → **Run anyway** (the installer isn't code-signed yet).
3. Click **Install**, then **Finish**. Leave **Start OBS now** ticked and OBS opens with the Tandem docks.

The installer:
- needs no administrator rights and installs into `C:\ProgramData\obs-studio\plugins\tandem`, where OBS finds plugins for every user;
- asks you to close OBS if it's open, and warns if OBS isn't installed or is older than 32.2;
- updates an existing Tandem in place (your settings and keys are kept);
- adds **Tandem for OBS Studio** to Windows **Settings → Apps → Installed apps**, which is where you remove it.

**Zip instead of the installer:** extract `tandem-<version>-windows-x64.zip` into your OBS folder (usually `C:\Program Files\obs-studio`) so that `obs-plugins\64bit\tandem.dll` and `data\obs-plugins\tandem\` land next to OBS's own plugins. Close OBS first.

## Checking a download (optional)

Every [release](https://github.com/Riqqqque/tandem/releases) lists the SHA-256 checksum of each file. Release files are built by GitHub Actions from the tagged source. To check yours:

- Windows (PowerShell): `Get-FileHash .\tandem-windows-installer.exe -Algorithm SHA256`
- macOS: `shasum -a 256 tandem-macos.pkg`
- Linux: `sha256sum tandem-<version>-x86_64-linux-gnu-ubuntu-24.04.deb`

The hash must match the one on the release page exactly. If an antivirus warns about the new, unsigned plugin, see [Troubleshooting](Troubleshooting.md#antivirus-warnings).

## macOS

[Download `tandem-macos.pkg`](https://github.com/Riqqqque/tandem/releases/latest/download/tandem-macos.pkg) and open it. It installs into `~/Library/Application Support/obs-studio/plugins`. On first start macOS may ask you to allow the plugin in System Settings → Privacy & Security.

Current status: the macOS build is produced by CI but has not been tested on a Mac yet. Stream keys are stored in the OBS profile folder on macOS (Keychain support is planned).

## Linux

- **Ubuntu / Debian:** install the `.deb` that matches your release with `sudo apt install ./tandem-<version>-x86_64-linux-gnu-ubuntu-24.04.deb` (or `-ubuntu-26.04`).
- **Flatpak OBS:** install the `.flatpak` bundle with `flatpak install --user ./tandem-<version>-flatpak-x86_64.flatpak`.

Current status: Linux builds are produced by CI but have not been tested on a desktop yet. Chat works with any libcurl that has TLS: when libcurl has no WebSocket support of its own (common on older distributions and on macOS), Tandem uses its built-in WebSocket client over libcurl's TLS connection. Stream keys are stored in the OBS profile folder on Linux.

## Coming from obs-multi-rtmp

Tandem reads `obs-multi-rtmp.json` from each OBS profile the first time it starts and imports your targets into `tandem.json` (keys move to Credential Manager on Windows). The old file is left as it was. Uninstall obs-multi-rtmp afterwards so both plugins don't run at once.

## Uninstall

- Windows: **Settings → Apps → Installed apps → Tandem for OBS Studio → Uninstall**. (Installed with Tandem 0.3.1 or older? Run the newest installer once so it shows up there, or delete `C:\ProgramData\obs-studio\plugins\tandem`.)
- Settings stay in each OBS profile as `tandem.json`; stored keys stay in Credential Manager under `Tandem/`. See [Privacy and security](Privacy-and-Security.md) to remove them.
