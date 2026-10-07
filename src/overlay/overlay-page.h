// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - the self-contained overlay page served at / and /overlay.
//
// No external assets, fonts or requests: the only network use is EventSource("/events") on
// the same origin, which the server's CSP enforces. Keep this ASCII-only. MSVC limits a single
// string literal piece to about 16 KB, so the page is split into adjacent raw-string pieces.
#pragma once

namespace tandem::overlay {

inline constexpr char kOverlayPage[] =
	R"TANDEMPAGE(<!DOCTYPE html>
<!--
Tandem chat overlay. Add as an OBS Browser Source pointing at this page.

URL parameters (all optional):
  max=30         messages kept on screen (1-200)
  fade=0         seconds before a message fades out (0 = never)
  icons=1        show the platform label (1/0)
  ts=0           show a timestamp (1/0)
  badges=1       show broadcaster/mod/vip tags (1/0)
  platforms=     comma list filter, e.g. twitch,kick (default: all)
  font=18        font size in px (8-96)
  theme=dark     dark | light | none (none = no backgrounds, no outline)
  align=left     left | right

Stable class names for OBS "Custom CSS":
  #chat                     the message list (newest at the bottom)
  .msg                      one message row; also .msg.twitch / .msg.kick / .msg.youtube
  .msg.fading               added when a message starts fading out
  .platform                 platform label pill; .platform.twitch / .platform.kick / .platform.youtube
  .badge                    role tag (.badge.broadcaster, .badge.moderator, .badge.vip)
  .author                   display name (inline color is the user's chat color)
  .sep                      the ":" after the name
  .text                     message text
  .time                     timestamp (when ts=1)
  body.theme-dark / body.theme-light / body.theme-none, body.align-right
Example: .platform { display: none; }  .msg { background: none; }
-->
<html>
<head>
<meta charset="utf-8">
<meta name="referrer" content="no-referrer">
<title>Tandem chat overlay</title>
<style>
html, body {
	margin: 0;
	padding: 0;
	background: transparent;
	overflow: hidden;
	height: 100%;
}
body {
	font-family: "Segoe UI", system-ui, -apple-system, "Helvetica Neue", Arial, sans-serif;
	font-size: 18px;
	line-height: 1.35;
	color: #fff;
}
#chat {
	position: absolute;
	left: 0;
	right: 0;
	bottom: 0;
	max-height: 100%;
	display: flex;
	flex-direction: column;
	justify-content: flex-end;
	padding: 6px 8px;
	box-sizing: border-box;
	overflow: hidden;
}
.msg {
	margin: 3px 0;
	padding: 3px 8px;
	border-radius: 8px;
	overflow-wrap: anywhere;
	word-break: break-word;
	transition: opacity 0.6s ease;
	opacity: 1;
	align-self: flex-start;
	max-width: 100%;
	box-sizing: border-box;
}
body.align-right #chat { text-align: right; }
body.align-right .msg { align-self: flex-end; }
.msg.fading { opacity: 0; }
.platform {
	display: inline-block;
	font-size: 0.68em;
	font-weight: 700;
	letter-spacing: 0.03em;
	text-transform: uppercase;
	padding: 1px 6px;
	margin-right: 6px;
	border-radius: 999px;
	vertical-align: 0.12em;
	color: #fff;
	text-shadow: none;
}
.platform.twitch { background: #6f55c9; }
.platform.kick { background: #2c8a46; }
.platform.youtube { background: #c23b3b; }
.badge {
	display: inline-block;
	font-size: 0.62em;
	font-weight: 700;
	text-transform: uppercase;
	padding: 1px 5px;
	margin-right: 5px;
	border-radius: 4px;
	vertical-align: 0.15em;
	background: rgba(255, 255, 255, 0.22);
	color: #fff;
	text-shadow: none;
}
.badge.broadcaster { background: #b8860b; }
.badge.moderator { background: #2f6fb3; }
.badge.vip { background: #a8447e; }
.time {
	font-size: 0.75em;
	opacity: 0.75;
	margin-right: 6px;
	font-variant-numeric: tabular-nums;
}
.author { font-weight: 700; }
.sep { margin-right: 0.35em; }
.text { white-space: pre-wrap; }

body.theme-dark { color: #fff; }
body.theme-dark .msg { background: rgba(0, 0, 0, 0.45); }
body.theme-dark .author, body.theme-dark .text, body.theme-dark .sep, body.theme-dark .time {
	text-shadow: 0 0 2px #000, 0 0 2px #000, 1px 1px 2px rgba(0, 0, 0, 0.9);
}
body.theme-light { color: #111; }
body.theme-light .msg { background: rgba(255, 255, 255, 0.78); }
body.theme-light .author, body.theme-light .text, body.theme-light .sep, body.theme-light .time {
	text-shadow: 0 0 2px #fff, 0 0 2px #fff;
}
body.theme-light .badge { background: rgba(0, 0, 0, 0.45); }
body.theme-none .msg { background: none; padding: 1px 0; border-radius: 0; }
</style>
</head>
<body class="theme-dark">
<div id="chat" aria-live="polite"></div>
)TANDEMPAGE"
	R"TANDEMPAGE(<script>
(function () {
	"use strict";
	var params = new URLSearchParams(window.location.search);
	function num(name, def, lo, hi) {
		var v = parseFloat(params.get(name));
		if (!isFinite(v)) return def;
		return Math.min(hi, Math.max(lo, v));
	}
	function flag(name, def) {
		var v = params.get(name);
		if (v === null || v === "") return def;
		return v === "1" || v.toLowerCase() === "true" || v.toLowerCase() === "yes";
	}
	var cfg = {
		max: Math.round(num("max", 30, 1, 200)),
		fade: num("fade", 0, 0, 86400),
		icons: flag("icons", true),
		ts: flag("ts", false),
		badges: flag("badges", true),
		font: num("font", 18, 8, 96),
		theme: (params.get("theme") || "dark").toLowerCase(),
		align: (params.get("align") || "left").toLowerCase(),
		platforms: null
	};
	if (["dark", "light", "none"].indexOf(cfg.theme) < 0) cfg.theme = "dark";
	var plist = (params.get("platforms") || "").toLowerCase().split(",").map(function (s) { return s.trim(); })
		.filter(function (s) { return s.length > 0; });
	if (plist.length > 0) cfg.platforms = plist;

	document.body.className = "theme-" + cfg.theme + (cfg.align === "right" ? " align-right" : "");
	document.body.style.fontSize = cfg.font + "px";

	var chat = document.getElementById("chat");
	var byKey = new Map();
	var LABELS = { twitch: "Twitch", kick: "Kick", youtube: "YouTube" };
	var BADGES = { broadcaster: "host", owner: "host", moderator: "mod", vip: "vip" };
	var FALLBACK = ["#ff7f50", "#1e90ff", "#9acd32", "#ff69b4", "#daa520", "#00bfff", "#ba55d3", "#3cb371",
		"#ff4500", "#5f9ea0", "#d2691e", "#8a2be2"];

	function hexToRgb(c) {
		var m = /^#([0-9a-f]{6})$/i.exec(c || "");
		if (!m) return null;
		var n = parseInt(m[1], 16);
		return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
	}
	function lum(rgb) {
		var a = rgb.map(function (v) {
			v /= 255;
			return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
		});
		return 0.2126 * a[0] + 0.7152 * a[1] + 0.0722 * a[2];
	}
	function mix(rgb, target, t) {
		return rgb.map(function (v, i) { return Math.round(v + (target[i] - v) * t); });
	}
	function toHex(rgb) {
		return "#" + rgb.map(function (v) { return ("0" + v.toString(16)).slice(-2); }).join("");
	}
	function hashColor(name) {
		var h = 0;
		for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) | 0;
		return FALLBACK[Math.abs(h) % FALLBACK.length];
	}
	// Keep author colors readable: lift dark colors on the dark theme, darken light ones on
	// the light theme.
	function readableColor(color, name) {
		var rgb = hexToRgb(color) || hexToRgb(hashColor(name || ""));
		if (cfg.theme === "light") {
			for (var i = 0; i < 10 && lum(rgb) > 0.30; i++) rgb = mix(rgb, [0, 0, 0], 0.2);
		} else {
			for (var j = 0; j < 10 && lum(rgb) < 0.22; j++) rgb = mix(rgb, [255, 255, 255], 0.2);
		}
		return toHex(rgb);
	}
	function pad(n) { return (n < 10 ? "0" : "") + n; }
	function el(tag, cls, text) {
		var e = document.createElement(tag);
		if (cls) e.className = cls;
		if (text !== undefined) e.textContent = String(text);
		return e;
	}
	function allowed(platform) {
		return !cfg.platforms || cfg.platforms.indexOf(platform) >= 0;
	}
	function removeNode(node) {
		if (!node) return;
		if (node._fadeTimer) clearTimeout(node._fadeTimer);
		if (node._removeTimer) clearTimeout(node._removeTimer);
		byKey.delete(node._key);
		if (node.parentNode) node.parentNode.removeChild(node);
	}
	function trim() {
		while (chat.children.length > cfg.max) removeNode(chat.firstElementChild);
	}

	function addMessage(m) {
		if (!m || typeof m !== "object") return;
		var platform = String(m.platform || "");
		if (!LABELS.hasOwnProperty(platform) || !allowed(platform)) return;
		var key = platform + ":" + String(m.id || "");
		if (m.id && byKey.has(key)) return; // replayed history after a reconnect

		var row = el("div", "msg " + platform);
		row._key = key;
		row.dataset.platform = platform;
		row.dataset.authorId = String(m.authorId || "");
		if (cfg.ts) {
			var d = new Date(Number(m.ts) || Date.now());
			row.appendChild(el("span", "time", pad(d.getHours()) + ":" + pad(d.getMinutes())));
		}
		if (cfg.icons) row.appendChild(el("span", "platform " + platform, LABELS[platform]));
		if (cfg.badges && Array.isArray(m.badges)) {
			var seen = {};
			m.badges.forEach(function (b) {
				b = String(b);
				if (!BADGES.hasOwnProperty(b) || seen[BADGES[b]]) return;
				seen[BADGES[b]] = true;
				var cls = b === "owner" ? "broadcaster" : b;
				row.appendChild(el("span", "badge " + cls, BADGES[b]));
			});
		}
		var author = el("span", "author", m.author || "");
		if (cfg.theme !== "none")
			author.style.color = readableColor(m.color, String(m.author || ""));
		else if (/^#[0-9a-f]{6}$/i.test(m.color || ""))
			author.style.color = m.color;
		row.appendChild(author);
		row.appendChild(el("span", "sep", ":"));
		row.appendChild(el("span", "text", m.text || ""));

		chat.appendChild(row);
		if (m.id) byKey.set(key, row);
		if (cfg.fade > 0) {
			row._fadeTimer = setTimeout(function () {
				row.classList.add("fading");
				row._removeTimer = setTimeout(function () { removeNode(row); }, 700);
			}, cfg.fade * 1000);
		}
		trim();
	}
	function forEachRow(fn) {
		Array.prototype.slice.call(chat.children).forEach(fn);
	}

	function parse(ev) {
		try { return JSON.parse(ev.data); } catch (e) { return null; }
	}

	var es = null;
	var backoff = 1000;
	var retryTimer = null;
	function connect() {
		retryTimer = null;
		if (es) { try { es.close(); } catch (e) {} }
		var url = "/events?max=" + cfg.max;
		es = new EventSource(url);
		es.onopen = function () { backoff = 1000; };
		es.onerror = function () {
			// EventSource retries on its own while CONNECTING; it gives up (CLOSED) on errors
			// such as a non-200 reply, so reconnect manually with backoff.
			if (es.readyState === 2 && !retryTimer) {
				retryTimer = setTimeout(connect, backoff);
				backoff = Math.min(backoff * 2, 10000);
			}
		};
		es.addEventListener("message", function (ev) { addMessage(parse(ev)); });
		es.addEventListener("delete", function (ev) {
			var d = parse(ev);
			if (d) removeNode(byKey.get(String(d.platform) + ":" + String(d.id)));
		});
		es.addEventListener("clearuser", function (ev) {
			var d = parse(ev);
			if (!d || !d.authorId) return;
			forEachRow(function (row) {
				if (row.dataset.platform === String(d.platform) && row.dataset.authorId === String(d.authorId))
					removeNode(row);
			});
		});
		es.addEventListener("clearall", function (ev) {
			var d = parse(ev);
			if (!d) return;
			forEachRow(function (row) {
				if (!d.platform || row.dataset.platform === String(d.platform)) removeNode(row);
			});
		});
		es.addEventListener("status", function () {});
	}
	connect();
})();
</script>
</body>
</html>
)TANDEMPAGE";

} // namespace tandem::overlay
