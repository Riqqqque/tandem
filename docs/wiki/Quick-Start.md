# Quick start

## 1. Stream to one platform

If OBS already streams to one platform (Settings → Stream), keep it. That is your main stream. Tandem adds the others.

If you would rather manage every platform in Tandem, that works too; see "All platforms in Tandem" below.

## 2. Add a second platform

1. In the **Tandem** dock, click **Add new target**.
2. **Name**: anything, e.g. "Kick".
3. **Platform**: pick Kick, Twitch, YouTube or Custom. The server URL is filled in for you.
4. **Stream key**: paste the key from the platform's dashboard. [Where to find it](Platform-Setup.md).
5. **Video/Audio encoder**: leave "Reuse the streaming encoder as OBS". This is what keeps the CPU/GPU cost near zero.
6. Click **OK**.
7. Tick the checkbox next to the target in the dock ("go live with OBS").

Click **Start Streaming** in OBS. OBS's own stream starts, and so do all ticked targets. **Stop Streaming** stops them too.

You can also start or stop a single target with its own **Start** button, or all ticked targets with **Start enabled**.

## 3. Add more

Repeat for the third platform. The dock shows the planned upload ("about 13.5 Mbps for 3 outputs"); make sure your connection can carry it. See [Bandwidth and performance](Bandwidth-and-Performance.md).

## All platforms in Tandem

You can point OBS's own stream at one platform and add the other two in Tandem (simplest), or add all three in Tandem:

- Targets that reuse OBS's streaming encoder need OBS's stream to be running. If OBS's own stream is not supposed to go anywhere, give one target its own encoder, and let the others **Share from** that target. They will still share a single encode.

## Testing without going live

- **Twitch:** tick **Bandwidth test mode** in the target's settings. The stream reaches Twitch but is not shown to viewers.
- **YouTube:** create an unlisted or private stream in YouTube Studio and use its key.
- **Kick:** stream at an off time or to a test channel.

## Chat

1. In the **Tandem Chat** dock, click **Settings**.
2. Tick **Read chat** for each platform and enter the channel (Twitch name, Kick slug, YouTube video URL plus your API key).
3. Click **OK**. Chat starts automatically when you start streaming, or click **Start chat** now.

See [Unified chat dock](Unified-Chat-Dock.md) and [Chat overlay](Chat-Overlay.md).
