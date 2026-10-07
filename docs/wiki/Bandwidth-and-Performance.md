# Bandwidth and performance

## Upload math

Every output sends its own copy of the stream, so:

**upload needed ≈ (video bitrate + audio bitrate) × number of outputs**

| Setup | Upload needed |
|---|---|
| 1 platform at 6,000 + 160 kbps | about 6.2 Mbps |
| 2 platforms | about 12.3 Mbps |
| 3 platforms | about 18.5 Mbps |

Keep at least 25-30% headroom above that. Upload speed varies during the day, and when it dips, outputs start dropping frames.

The Tandem dock shows the planned upload for everything that is live or ticked, and the current measured upload while streaming. In **Settings → Streaming** you can enter your upload capacity; the dock turns red when the plan goes above 80% of it.

## Checking your upload speed

- Run a speed test (for example speed.cloudflare.com or fast.com) at the time of day you usually stream, on the same wired connection. Look at **upload**, not download.
- Better: start a real test (Twitch bandwidth test mode, an unlisted YouTube stream) with all targets for 10 minutes and watch each target's dropped-frames counter in the dock. Zero dropped frames means you're fine.

## Shared vs. separate encoders

| | Shared (default) | Separate encoder per target |
|---|---|---|
| CPU/GPU | One encode for every output | One extra encode per target |
| Bandwidth | Same | Same |
| Settings | Same resolution/bitrate everywhere | Different resolution, bitrate or scene per platform |
| Requirement | OBS's own stream (or recording) must be running | Works on its own |

Pick "Reuse the streaming encoder as OBS" unless a platform really needs different settings. Several Tandem targets can also **Share from** one another, so three targets with their own settings still only cost one extra encode.

## Recommended settings

- **Codec:** H.264 for Twitch and Kick (their RTMP ingest doesn't accept HEVC/AV1). YouTube also takes HEVC and AV1.
- **Keyframe interval:** 2 seconds (all three platforms recommend it).
- **Rate control:** CBR.
- **Bitrate:** whatever your upload allows after the math above; 6,000 kbps 1080p60 or 4,500 kbps 720p60 are common. Twitch's limit for non-partners is about 6,000-8,000 kbps.
- **Hardware encoder** (NVENC, AMF, QuickSync) if your CPU is busy with games.

## Measured overhead

On the development machine (Ryzen 7 9800X3D, OBS 32.2.2, x264 veryfast 1280×720 30 fps at 2,500 kbps, local RTMP receivers, same scene, 2-minute runs):

| Run | OBS CPU (avg) | OBS process CPU time | Skipped/dropped frames | Memory |
|---|---|---|---|---|
| OBS stream only, Tandem not installed | 1.70% | 34.4 s | 0 | ~527 MB |
| OBS stream + 3 Tandem targets sharing the encoder + Twitch and Kick chat + overlay | 1.85% | 37.3 s | 0 | ~513 MB |

Every output received all 3,653 frames. The difference is the network sending (three extra copies) and chat parsing; no extra encoding happens.

## How Tandem keeps OBS smooth

- All chat networking and parsing runs on Tandem's own threads, never on OBS's video, audio, graphics or UI threads.
- Chat flows through bounded queues; under a flood the oldest unread messages are dropped instead of memory growing.
- The dock redraws at most 10 times per second.
- Outputs are plain OBS outputs; OBS's own networking and congestion handling apply to them.
