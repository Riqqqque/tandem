# FAQ

**Does streaming to three platforms triple my CPU or GPU load?**
No, if the targets reuse OBS's encoder (the default). One encode feeds every output. Your upload bandwidth does triple.

**Can I stream to just one or two platforms?**
Yes. Every target and every chat platform is opt-in. Untick or remove what you don't use.

**Do I need accounts or a paid relay service?**
No. Tandem connects directly from your computer to each platform. Twitch and Kick chat need no login; YouTube chat needs a free API key from your own Google account.

**Can I send chat messages from the dock?**
Not yet. The chat is read-only for now.

**Can each platform get different quality?**
Yes. Give a target its own encoder with its own resolution, bitrate or even a different scene. That costs an extra encode. Twitch's rules say Twitch must not get worse quality than other platforms.

**Can I use vertical video for one platform?**
Yes, if you have a vertical scene: give that target its own encoder and pick the scene in its video settings.

**Is Kick chat official?**
No. Kick's official chat API needs a public web server, which a desktop plugin can't provide, so Tandem uses the same public feed kick.com itself uses. It works, but Kick can change it.

**Where are my stream keys stored?**
On Windows, in Windows Credential Manager. Elsewhere, in the OBS profile folder. Never in the log. See [Privacy and security](Privacy-and-Security.md).

**I used obs-multi-rtmp. Do I have to set everything up again?**
No. Tandem imports your targets from the same OBS profile the first time it starts.

**Does Tandem collect any data?**
No telemetry, no analytics, no update checks.

**Why is it called Tandem?**
Several streams moving together, driven by one encoder.
