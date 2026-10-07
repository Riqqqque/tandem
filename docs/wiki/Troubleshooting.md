# Troubleshooting

## Reading the OBS log

Help → Log Files → **Show Log Files** (or **Upload Current Log File** to share it). Tandem's lines start with `[tandem]`; its outputs appear as `Tandem: <name> (<id>)`. Stream keys are never written to the log. Before sharing a log anyway, look for anything you consider private.

## The Tandem docks are missing

Docks menu → tick **Tandem** and **Tandem Chat**. If they aren't listed, check the log for `[tandem] version ... loaded`. If it isn't there, the plugin didn't load. Reinstall it, and make sure your OBS is 32.2 or newer.

## "This target reuses OBS's encoder, which only runs while OBS itself is streaming or recording"

A target set to "Reuse the streaming encoder as OBS" can only start while OBS's own stream is running. Either start OBS's stream (ticked targets then start with it), or give the target its own encoder.

## Reconnect loops / "Stopped: the server kept dropping the connection right after connecting"

The server accepted the connection and then closed it, five times within a few minutes. Common causes:
- **Wrong or expired stream key.** Copy it again from the platform's dashboard. Some platforms accept the connection before checking the key.
- **Codec.** Twitch and Kick need H.264 over RTMP. HEVC or AV1 connects and then gets dropped. Tandem shows a warning on the target when this applies.
- **Upload too low.** If the dock shows many dropped frames before the disconnect, lower the bitrate or stream to fewer platforms.
- **The same key used twice**, for example by OBS's own stream and a Tandem target, or by two computers.

## RTMP errors

| Message | Meaning |
|---|---|
| incorrect RTMP address | The URL is malformed; use the preset or copy it again |
| failed to connect to server | Network, firewall, VPN or a wrong server; try another network or turn the VPN off |
| failed to connect to stream | The server rejected the stream; usually the key |
| connection refused by server | Server-side rejection; check the platform's dashboard for warnings |
| Disconnected from the server | Lost after streaming; OBS retries using Settings → Advanced → Automatically Reconnect |

Targets use the retry delay and maximum retries from OBS's **Settings → Advanced → Automatically Reconnect**.

## Antivirus warnings

New, unsigned plugin files are sometimes flagged by antivirus heuristics. Download only from this repository's Releases page and compare the SHA-256 checksum with the one on the release. If it matches and your antivirus still complains, report it to your antivirus vendor as a false positive and open an issue so it can be tracked.

## Kick chat doesn't connect

- "Kick blocked the channel lookup": Kick sometimes challenges automated requests. Enter the **Chatroom ID** in Settings (see [Platform setup](Platform-Setup.md#kick)) so the lookup isn't needed.
- Connected but no messages: the channel may be offline or quiet.
- Errors after a Kick website update: the unofficial feed may have changed. Check for a Tandem update and open an issue with the log lines starting `[tandem] [chat/kick]`.

## Twitch chat

- "Invalid Twitch channel name": use only the name from `twitch.tv/<name>`, without spaces or other symbols.
- No messages: the channel may be quiet, or set to followers-only/subscribers-only chat (reading still works; it's just quiet).

## YouTube chat

| Status | What to do |
|---|---|
| YouTube API key is not valid | Copy the key again; make sure the YouTube Data API v3 is enabled for that project |
| Waiting for the stream to go live | Normal before you go live; Tandem checks every minute |
| YouTube API quota exhausted for today | The free daily quota is used up; it resets at midnight Pacific time. Use the video URL instead of a channel ID, and avoid restarting chat repeatedly |
| YouTube Data API v3 is not enabled for this key's Google Cloud project | Enable the API under APIs & Services → Library |
| YouTube API key is restricted and cannot call the YouTube Data API | Edit the key's API restrictions to allow YouTube Data API v3 |
| Live chat has ended / This live stream has ended | The broadcast ended, or chat was turned off for it |
| Live chat is disabled for this stream | Chat is turned off in YouTube Studio |
| YouTube refused access to this live chat | Chat is limited, for example members-only |
| This video is not a live stream | Use the URL of the live broadcast, not a regular video |

## Overlay shows nothing

- Tandem Chat → Settings → Overlay should say "running". If it says the port is in use, pick another port and update the Browser Source URL.
- The Browser Source URL must start with `http://127.0.0.1:<port>/` (or `localhost`).
- Is chat running? The overlay shows only what the dock receives.
- Check `platforms=` in the URL.
