# Privacy

Tandem has no telemetry, analytics, crash reporting or update checks. It has no server of its own and no accounts.

## What stays on your computer

- **Stream keys, stream passwords and WHIP tokens**: on Windows they are saved in Windows Credential Manager under `Tandem/target/<id>/...`. On macOS and Linux they are saved in the OBS profile folder (`tandem.json`), like OBS stores its own stream key.
- **YouTube API key**: Windows Credential Manager (`Tandem/chat/youtube/api-key`) on Windows, otherwise the OBS profile folder.
- **Settings** (targets, chat channels, overlay options, the YouTube quota estimate): `tandem.json` in each OBS profile folder.
- **Chat messages** are kept in memory only (500 by default) and are never written to disk.
- Keys are never written to the OBS log.

## What Tandem connects to, and why

Only when you turn the matching feature on:

| Feature | Endpoint | Why |
|---|---|---|
| Streaming targets | The RTMP/RTMPS/SRT/WHIP server URL you configure (for presets: Twitch `ingest.global-contribute.live-video.net`, Kick `fa723fc1b171.global-contribute.live-video.net`, YouTube `a.rtmps.youtube.com`) | Sends your stream |
| Twitch chat | `wss://irc-ws.chat.twitch.tv` | Reads public chat anonymously (no login) |
| Kick chat | `https://kick.com/api/v2/channels/<slug>` and `wss://ws-us2.pusher.com` | Looks up the chat room, then reads public chat (the same feed kick.com uses) |
| YouTube chat | `https://www.googleapis.com/youtube/v3/...` and `https://youtube.googleapis.com/youtube/v3/liveChat/messages/stream` | Finds the live chat and reads it with your API key, sent in the `X-Goog-Api-Key` header (never in the URL) |
| Overlay | Listens on `127.0.0.1` only | Serves the overlay page to OBS's Browser Source on the same computer |

Those platforms see the same thing a normal viewer's browser would: your IP address and the channel you open. Their privacy policies apply to that traffic.

## Removing your data

- Delete `tandem.json` from your OBS profile folders.
- On Windows, open Credential Manager → Windows Credentials and remove the entries starting with `Tandem/`, or run `cmdkey /list | findstr Tandem` and `cmdkey /delete:<name>`.
