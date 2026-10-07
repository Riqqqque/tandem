# Changelog

All notable changes to Tandem are listed here. Versions follow [semantic versioning](https://semver.org).

## [Unreleased]

## [0.1.0] - 2026-10-07

First release of Tandem, forked from obs-multi-rtmp 0.7.4.4.

### Added
- Platform presets for Twitch, Kick and YouTube that fill in the RTMPS ingest URL, with a hint on where to find each stream key.
- **Import from OBS** copies the server and key from OBS's own stream settings.
- Per-target "go live with OBS" checkbox in the dock; **Start enabled** starts only ticked targets.
- Twitch bandwidth-test mode per target (`?bandwidthtest=true`), so test runs are not shown to viewers.
- Upload estimate for all live and enabled outputs, a note when targets use their own encoders, and an optional upload-capacity warning.
- Dropped-frame count and the server's error text in each target's status line.
- Stream keys, stream passwords, WHIP tokens and the YouTube API key are stored in Windows Credential Manager. Keys found in older plain-text configs are moved there on first start, and the plain-text backup file is removed.
- Unified chat dock for Twitch (anonymous, read-only IRC over secure WebSocket), Kick (public chat feed) and YouTube (your own Data API key; HTTP streaming with polling fallback). Per-platform status, filters, timestamps, deletes and bans, 500-message cap, batched updates.
- Estimated YouTube API quota meter that follows Google's Pacific-time reset; YouTube chat stops cleanly with a clear message when the quota runs out.
- Optional chat overlay for an OBS Browser Source, served on `127.0.0.1` only, styleable with URL options and Custom CSS.
- Settings dialog with Streaming, Chat and Overlay tabs.
- Unit tests for the chat parsers, the reconnect backoff, the chat hub and the overlay server, plus a command-line chat probe.
- Docks open the first time Tandem loads.
- Targets from obs-multi-rtmp (`obs-multi-rtmp.json`) are imported on first start; the old file is not changed.

### Fixed (reported against obs-multi-rtmp)
- Endless reconnect loop when a server accepts the connection and then drops it, often reported with Twitch and Kick. OBS resets its retry counter after every successful connection, so this never ended. Tandem now stops after 5 quick drops within 5 minutes and explains why (sorayuki/obs-multi-rtmp#513, #458, #473, #477, #555).
- Targets ignored the reconnect settings in OBS's Advanced settings; they now use the same retry delay and maximum retries as OBS's own stream (#562, #470).
- A target that was still connecting could not be stopped: clicking Stop restarted it, and stopping on exit or profile change skipped it (#39, #470, #573).
- Sync stop did not trigger when the server dropped OBS's main stream (#557).
- Extra audio tracks (Twitch VOD track) leaked their encoders, so mixer and encoder changes only applied after restarting OBS (#416, #510).
- Changing the encoder type of a shared encoder config was ignored while the old encoder was cached by name (#566).
- Encoders were released inside the output's stop signal, while the output still counted as active, so the release was skipped; cleanup now runs on the UI thread after the output deactivates.
- UI callbacks from OBS's output threads could touch a deleted target widget after a profile switch or delete (crash reports in #573, #558).
- Old target widgets kept running after a profile switch.
- Renaming or listing profiles force-stopped every running target.
- Every output was called `multi-output`, so the OBS log and obs-websocket could not tell them apart; outputs are now named `Tandem: <name> (<id>)` (#326, #595).
- "Failed to start output" hid the real reason; the encoder or server error is now shown and logged (#367, #402, #397).
- WHIP outputs with their own encoder could stay black with NVENC because the service's required encoder settings (no B-frames, repeated headers) were never applied (#488).
- HEVC/AV1 on plain RTMP to Twitch or Kick now shows a warning instead of silently looping (#513, #559).
- The edit dialog's Cancel kept encoder changes made while it was open.
- Changing one combo box in the edit dialog could change another one through re-entrant updates (#575).
- A `QLayout` double-parenting warning and possible crash when opening the edit dialog (#560, #583).
- A new `QMenu` was leaked every time "Share from" was clicked (#573).
- A misleading "audio encoder config failed" error was logged when audio was shared with OBS (#523).
- Float properties were saved as integers, and a missing `break` in the property widget.
- The output settings object was leaked on every start.
- "Shared with Not shared" label when a target reuses OBS's encoder.
- Calls into OBS's frontend while OBS was still loading modules could crash on start.
- The Windows uninstaller removed the entire `ProgramData\obs-studio\plugins` folder, deleting every other plugin. It now removes only Tandem's folder.

### Changed
- Renamed to Tandem: module `tandem`, docks `tandem-outputs-dock` and `tandem-chat-dock`, config file `tandem.json`, Flatpak id `com.obsproject.Studio.Plugin.Tandem`.
- nlohmann/json updated from 3.11.2 to 3.12.0.
- Build template fixes: the Windows install path, a missing build number on the first macOS configure, and the Ubuntu packaging debug flag.
- Removed the upstream donation dialog and homepage files; upstream is credited in the dock and in CREDITS.md.
