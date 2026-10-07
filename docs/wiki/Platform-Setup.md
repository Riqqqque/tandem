# Platform setup

## Twitch

**Streaming**
- Preset server: `rtmps://ingest.global-contribute.live-video.net:443/app` (Twitch's global ingest, chooses the nearest server automatically).
- Stream key: Twitch Creator Dashboard → Settings → Stream → Primary Stream Key. Treat it like a password.
- Video codec: H.264. Twitch's regular ingest does not accept HEVC or AV1 over RTMP; Tandem warns if you pick one.
- **Bandwidth test mode** (target settings): Tandem adds `?bandwidthtest=true` to the key when it connects, so the stream reaches Twitch without being shown to viewers. Turn it off to go live for real.

**Chat**
- Enter the channel name (as in `twitch.tv/<name>`). No login or token is needed: Tandem joins chat anonymously and read-only.

## Kick

**Streaming**
- Preset server: `rtmps://fa723fc1b171.global-contribute.live-video.net:443/app`.
- Kick shows your own Stream URL and Stream Key in the Creator Dashboard → Settings → Stream URL & Key. If the URL there differs from the preset, paste Kick's URL instead.
- Video codec: H.264.

**Chat**
- Enter your channel slug (as in `kick.com/<slug>`).
- Tandem reads chat from the same public feed that kick.com itself uses. Kick has no official chat API that a desktop app can use (its official chat events need a public web server), so this feed is unofficial and Kick can change or block it without notice. If the chat dock shows an error after a Kick update, check for a Tandem update.
- **Chatroom ID** (optional): Tandem looks up the chat room from the slug. If that lookup is blocked (Kick sometimes rate-limits or challenges it), enter the numeric chatroom ID yourself. You can find it by opening `https://kick.com/api/v2/channels/<slug>` in a browser and looking for `"chatroom":{"id": ...}`.

## YouTube

**Streaming**
- Preset server: `rtmps://a.rtmps.youtube.com:443/live2`.
- Stream key: YouTube Studio → Create → Go live → Stream → Stream key.
- YouTube requires a verified channel with live streaming enabled (the first time can take 24 hours).

**Chat**

YouTube chat needs a free API key from your own Google account. It takes about five minutes:

1. Open the [Google Cloud console](https://console.cloud.google.com/) and create a project (any name, e.g. "Tandem").
2. Go to **APIs & Services → Library**, search for **YouTube Data API v3** and click **Enable**.
3. Go to **APIs & Services → Credentials → Create credentials → API key**.
4. Recommended: click the new key → **API restrictions → Restrict key → YouTube Data API v3**. This way the key can't be used for anything else.
5. Copy the key into Tandem: Tandem Chat dock → Settings → YouTube → API key.

Then enter your live video: the URL (`youtube.com/watch?v=...`, `youtube.com/live/...`, `youtu.be/...`) or the 11-character video ID. Entering a channel ID (`UC...`) instead also works, but finding the live video that way is expensive (see quota below), so prefer the video URL.

If the stream isn't live yet, Tandem waits and checks again every minute (every 5 minutes for a channel ID).

**Quota, in plain words**

Google gives each project 10,000 "units" per day for free. Reading chat uses some of them:

| What | Estimated cost |
|---|---|
| Finding the chat of a video | 1 unit |
| Finding the live video of a channel | 100 units (and at most 100 searches per day) |
| Opening the live chat stream | about 5 units per (re)connect |
| Each poll, if streaming isn't available | about 5 units |

Tandem uses YouTube's streaming chat connection when it can, which is far cheaper than polling. The settings dialog shows an estimate of today's use. The count resets at midnight Pacific time, when Google resets it. If the quota does run out, YouTube chat stops with "YouTube API quota exhausted for today"; the other platforms and your stream keep going. You can check real usage under APIs & Services → YouTube Data API v3 → Quotas in the Google Cloud console.

The key is sent only in a request header to Google's servers, never in a URL, and never written to the log.

## Custom servers

Choose **Custom** and enter the server URL and key from your service. RTMPS (`rtmps://`) works for any server that supports it. The **Protocol** box also offers SRT/RIST and WebRTC (WHIP).
