# Privacy and security

**Short version:** no telemetry, no accounts, no Tandem servers. Your keys stay on your computer, and Tandem connects only to the streaming and chat services you turn on.

## Where things are stored

| Data | Windows | macOS / Linux |
|---|---|---|
| Stream keys, stream passwords, WHIP tokens | Windows Credential Manager (`Tandem/target/<id>/...`) | `tandem.json` in the OBS profile folder |
| YouTube API key | Windows Credential Manager (`Tandem/chat/youtube/api-key`) | `tandem.json` in the OBS profile folder |
| Settings | `tandem.json` in each OBS profile folder | same |
| Chat messages | Memory only, never saved | same |

Keys are masked in the settings (click the eye or **Show** to reveal them) and never written to the OBS log.

If you used obs-multi-rtmp, its `obs-multi-rtmp.json` (which holds keys in plain text) is left untouched; delete it yourself once you've switched.

## Network connections

| When | Where | Why |
|---|---|---|
| A target is streaming | The server URL you set | Sends the stream |
| Twitch chat is on | `irc-ws.chat.twitch.tv` (secure WebSocket) | Reads public chat anonymously |
| Kick chat is on | `kick.com` (chat room lookup), `ws-us2.pusher.com` (secure WebSocket) | Reads public chat |
| YouTube chat is on | `www.googleapis.com`, `youtube.googleapis.com` | Reads live chat with your API key (header only) |
| Chat is on | 7TV, BetterTTV, FrankerFaceZ APIs and the emote image servers | Loads emote lists and images (extension emotes can be turned off) |
| Overlay is on | Listens on `127.0.0.1` | Serves the overlay page to your own OBS |

## Overlay server security

- Listens on `127.0.0.1` only; other devices can't reach it.
- Rejects requests whose `Host` header isn't `127.0.0.1:<port>` or `localhost:<port>`, so web pages you visit can't read it through DNS tricks.
- Serves only the overlay page and the event stream; read-only, no settings or keys.
- Chat text is inserted as plain text, and the page has a strict Content Security Policy.

## Removing everything

1. Uninstall Tandem.
2. Delete `tandem.json` from your OBS profile folders.
3. Windows: Credential Manager → Windows Credentials → remove entries beginning with `Tandem/` (or `cmdkey /list`, then `cmdkey /delete:<target>`).
4. If you created a Google Cloud project for the YouTube key, delete the key or the project in the Google Cloud console.

## Reporting a security problem

See [SECURITY.md](../../SECURITY.md). Please report privately through GitHub's "Report a vulnerability".
