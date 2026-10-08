// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - streaming platform presets. URLs change over time; keep them in one place.
#pragma once

#include <string>
#include <string_view>

namespace tandem {

struct PlatformPreset {
	const char *id;        // stored in the target config
	const char *label;     // shown in the UI (plain text, no logos)
	const char *server;    // RTMP(S) ingest URL; empty for custom
	bool h264Only;         // plain RTMP ingest only accepts H.264 video
	const char *keyHelp;   // where the user finds the stream key
	const char *keyUrl;    // page that shows the stream key; empty for custom
};

inline const PlatformPreset kPlatformPresets[] = {
	{"twitch", "Twitch", "rtmps://ingest.global-contribute.live-video.net:443/app", true,
	 "Twitch: Creator Dashboard > Settings > Stream > Primary Stream Key",
	 "https://dashboard.twitch.tv/settings/stream"},
	{"kick", "Kick", "rtmps://fa723fc1b171.global-contribute.live-video.net:443/app", true,
	 "Kick: Creator Dashboard > Channel > Stream URL & Key (copy the URL too if it differs)",
	 "https://dashboard.kick.com/channel/stream"},
	{"youtube", "YouTube", "rtmps://a.rtmps.youtube.com:443/live2", false,
	 "YouTube: YouTube Studio > Go live > Stream > Stream key",
	 "https://studio.youtube.com/"},
	{"custom", "Custom", "", false, "Enter the server URL and stream key from your service.", ""},
};

inline std::string HtmlEscape(std::string_view s)
{
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		default: out += c;
		}
	}
	return out;
}

// Help text plus a clickable link to the page that shows the key (rich text for a QLabel).
inline std::string KeyHelpHtml(const PlatformPreset &p, std::string_view linkText)
{
	std::string html = HtmlEscape(p.keyHelp);
	if (p.keyUrl && *p.keyUrl)
		html += " <a href=\"" + std::string(p.keyUrl) + "\">" + HtmlEscape(linkText) + "</a>";
	return html;
}

inline const PlatformPreset &FindPlatformPreset(std::string_view id)
{
	for (auto &p : kPlatformPresets) {
		if (id == p.id)
			return p;
	}
	return kPlatformPresets[3];
}

// Best-effort guess for targets created before presets existed.
inline const char *GuessPlatformFromServer(std::string_view server)
{
	auto has = [&](std::string_view s) { return server.find(s) != std::string_view::npos; };
	// Kick also ingests through IVS, so check its endpoint first.
	if (has("fa723fc1b171") || has("kick.com"))
		return "kick";
	if (has("twitch.tv") || has("contribute.live-video.net"))
		return "twitch";
	if (server.find("youtube.com") != std::string_view::npos)
		return "youtube";
	return "custom";
}

} // namespace tandem
