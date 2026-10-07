# Unified chat dock

The **Tandem Chat** dock shows chat from every platform you enabled, newest at the bottom. Each line starts with a platform label (Twitch, Kick, YouTube), then the author in their chat color, then the message. A star marks the channel owner, a sword marks moderators.

## Controls

- **Status / filter row** at the top: one checkbox per platform, showing its state:
  - `off`: chat for that platform is disabled in Settings.
  - `idle`: enabled but chat isn't running.
  - `connecting`, `connected`, `reconnecting`, `error`: hover for the reason (for example "Waiting for the stream to go live" or "YouTube API quota exhausted for today").
  - Untick a platform to hide its messages; ticking it again shows them.
- **Start chat / Stop chat**: start or stop all enabled platforms. By default chat starts by itself when OBS starts streaming and stops when it stops (Settings → Chat → "Start chat when OBS starts streaming").
- **Clear**: empties the dock and the history the overlay replays when it (re)connects. Messages already on an overlay stay until they fade or scroll away.
- **Settings**: platforms, channels, the YouTube key, timestamps, the message limit and the overlay.

## Behaviour and limits

- Read-only: Tandem doesn't send chat messages.
- Messages deleted by moderators disappear from the dock and overlay; a ban or timeout removes that user's messages; a chat clear removes that platform's messages.
- The dock keeps the newest 500 messages (adjustable from 50 to 5000), so memory stays flat on long streams.
- Updates are drawn every 100 ms in batches, so a busy chat doesn't slow OBS down. If you scroll up to read, the dock stops auto-scrolling until you scroll back to the bottom.
- Emotes are shown as their text names.
- Each platform runs on its own connection and thread. If one platform fails, the others keep working.
- Reconnects use exponential backoff with jitter (1 second up to 1 minute) and only reset after a connection has been healthy for 30 seconds, so a flaky network doesn't cause a reconnect storm. Permanent problems (invalid channel, invalid API key, quota exhausted, chat ended) stop with an error instead of retrying.
- Chat history from before Tandem connected isn't loaded.
