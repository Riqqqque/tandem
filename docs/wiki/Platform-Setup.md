# Platform setup

## Twitch

**Streaming**
- Preset server: `rtmps://ingest.global-contribute.live-video.net:443/app` (Twitch's global ingest, chooses the nearest server automatically).
- Stream key: [dashboard.twitch.tv/settings/stream](https://dashboard.twitch.tv/settings/stream) (Creator Dashboard → Settings → Stream → Primary Stream Key). Treat it like a password.
- Video codec: H.264. Twitch's regular ingest does not accept HEVC or AV1 over RTMP; Tandem warns if you pick one.
- **Bandwidth test mode** (target settings): Tandem adds `?bandwidthtest=true` to the key when it connects, so the stream reaches Twitch without being shown to viewers. Turn it off to go live for real.

**Chat**
- Enter the channel name (as in `twitch.tv/<name>`). No login or token is needed: Tandem joins chat anonymously and read-only.

## Kick

**Streaming**
- Preset server: `rtmps://fa723fc1b171.global-contribute.live-video.net:443/app`.
- Kick shows your own Stream URL and Stream Key at [dashboard.kick.com/channel/stream](https://dashboard.kick.com/channel/stream) (Creator Dashboard → Channel → Stream URL & Key). If the URL there differs from the preset, paste Kick's URL instead.
- Video codec: H.264.

**Chat**
- Enter your channel slug (as in `kick.com/<slug>`).
- Tandem reads chat from the same public feed that kick.com itself uses. Kick has no official chat API that a desktop app can use (its official chat events need a public web server), so this feed is unofficial and Kick can change or block it without notice. If the chat dock shows an error after a Kick update, check for a Tandem update.
- **Chatroom ID** (optional): Tandem looks up the chat room from the slug. If that lookup is blocked (Kick sometimes rate-limits or challenges it), enter the numeric chatroom ID yourself. You can find it by opening `https://kick.com/api/v2/channels/<slug>` in a browser and looking for `"chatroom":{"id": ...}`.

## YouTube

**Streaming**
- Preset server: `rtmps://a.rtmps.youtube.com:443/live2`.
- Stream key: [studio.youtube.com](https://studio.youtube.com/) → Create → Go live → Stream → Stream key.
- YouTube requires a verified channel with live streaming enabled (the first time can take 24 hours).

**Chat**

**Just enter your channel.** No API key, no Google Cloud, no login:

1. Tandem Chat dock → **Settings** → **YouTube** (or the YouTube card in Simple mode).
2. Tick **Read chat** (**Show this chat** in Simple mode).
3. In **Channel**, type your handle, for example `@yourname`, or paste your channel link (`youtube.com/@yourname`).

That's all. Tandem watches the channel and connects to the chat by itself whenever you go live, and picks up the next stream after one ends. Until then the dock shows **YouTube: waiting for live**. A link to one live video (`youtube.com/watch?v=...`, `youtube.com/live/...`, `youtu.be/...`) also works if you only want that stream.

Why your stream key isn't enough: the stream key only lets OBS *send* video to YouTube. It can't be used to read anything back, including chat.

How it works: Tandem reads the same public chat that youtube.com shows next to your stream, like a logged-out viewer. It asks for the newest messages every 1 to 5 seconds (faster when chat is busy) and shows each message once. Like Kick's chat feed, this is not an official API, so YouTube could change it; if YouTube chat stops working after a YouTube update, check for a Tandem update or use the official API below.

### Optional: official YouTube API key

Most people don't need this. Use it only if the normal YouTube chat stops working for you. It uses Google's official YouTube Data API with a free key from your own Google account and takes about five minutes:

1. Sign in to the [Google Cloud console](https://console.cloud.google.com/) with your Google account. The first time, accept the terms. No payment details are needed.
2. Create a project: open [Create project](https://console.cloud.google.com/projectcreate), name it `Tandem`, click **Create**, and wait until the notification says it's ready. Make sure the project picker at the top shows **Tandem**.
3. Turn on the YouTube API: open [YouTube Data API v3](https://console.cloud.google.com/apis/library/youtube.googleapis.com) and click **Enable**.
4. Create the key: open [Credentials](https://console.cloud.google.com/apis/credentials), click **+ Create credentials** → **API key**. Copy the key that appears (it starts with `AIza`).
5. Lock it down (recommended): in the same list click the key's name → under **API restrictions** pick **Restrict key** → tick **YouTube Data API v3** → **Save**. Now the key can't be used for anything else.
6. In Tandem: Tandem Chat dock → **Settings** → **YouTube** → tick **Use the official YouTube API instead** → paste the key → **OK**.

Your **Channel** entry stays the same. Tandem still finds your live stream through youtube.com (free), then reads the chat through the API.

**Quota, in plain words.** Google gives each project 10,000 free "units" per day. Tandem uses:

| What | Estimated cost |
|---|---|
| Finding the chat of your live video | 1 unit |
| Opening the live chat stream | about 5 units per (re)connect |
| Each poll, if streaming isn't available | about 5 units |

A normal day of streaming uses a small part of that. The settings show an estimate of today's use, which resets at midnight Pacific time, when Google resets it. If the quota runs out, YouTube chat stops with "YouTube API quota exhausted for today"; untick the API box to go back to the keyless chat. The key is sent only in a request header to Google's servers, never in a URL, and never written to the log. Unticking the box deletes the key from Windows Credential Manager.

## Custom servers

Choose **Custom** and enter the server URL and key from your service. RTMPS (`rtmps://`) works for any server that supports it. The **Protocol** box also offers SRT/RIST and WebRTC (WHIP).
