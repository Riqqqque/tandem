# Chat overlay

Tandem can serve the merged chat as a web page for an OBS Browser Source, so viewers see it on stream. It is off by default.

## Turn it on

1. Tandem Chat dock → **Settings** → **Overlay** tab.
2. Tick **Serve a chat overlay for a Browser Source**. Change the port if 48080 is taken.
3. Click **Copy** next to the URL, then **OK**.
4. In OBS, add a **Browser** source, paste the URL, and set the size (for example 500 × 800).

The server listens on `127.0.0.1` only, so nothing outside your computer can reach it.

## URL options

Add them to the URL, for example `http://127.0.0.1:48080/?max=15&fade=30&platforms=twitch,kick`.

| Option | Default | Meaning |
|---|---|---|
| `max` | 30 | Messages kept on screen (1-200) |
| `fade` | 0 | Seconds before a message fades out; 0 keeps it |
| `icons` | 1 | Show the platform label (`0` to hide) |
| `ts` | 0 | Show a timestamp (`1` to show) |
| `badges` | 1 | Show owner/mod/VIP tags |
| `emotes` | 1 | Show emotes as images (`0` shows their names as text) |
| `platforms` | all | Only these platforms, e.g. `twitch,youtube` |
| `font` | 18 | Font size in pixels (8-96) |
| `theme` | dark | `dark`, `light`, or `none` (no backgrounds or outline) |
| `align` | left | `left` or `right` |

## Custom CSS

Use the Browser Source's **Custom CSS** box. Stable class names:

| Selector | Element |
|---|---|
| `#chat` | The message list |
| `.msg` (`.msg.twitch`, `.msg.kick`, `.msg.youtube`) | One message |
| `.msg.fading` | A message that is fading out |
| `.platform` (`.platform.twitch` ...) | The platform label |
| `.badge` (`.badge.broadcaster`, `.badge.moderator`, `.badge.vip`) | Role tag |
| `.author` | Display name (its inline color is the user's chat color) |
| `.sep` | The `:` after the name |
| `.text` | Message text |
| `.time` | Timestamp |
| `.emote` | Emote image inside the text |
| `body.theme-dark`, `body.theme-light`, `body.theme-none`, `body.align-right` | Page modes |

Examples:

```css
/* No platform labels, no message backgrounds */
.platform { display: none; }
.msg { background: none; }

/* Bigger names */
.author { font-weight: 800; font-size: 1.1em; }

/* Kick messages in green */
.msg.kick .text { color: #53fc18; }
```

## Notes

- Message text is always inserted as plain text, so chat can't inject HTML into your overlay.
- The page reconnects by itself if Tandem or OBS restarts.
- Some platforms have rules about showing other platforms' chat on stream. See [Platform rules](Platform-Rules.md).
