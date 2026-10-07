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
};

inline const PlatformPreset kPlatformPresets[] = {
	{"twitch", "Twitch", "rtmps://ingest.global-contribute.live-video.net:443/app", true,
	 "Twitch: Creator Dashboard > Settings > Stream > Primary Stream Key"},
	{"kick", "Kick", "rtmps://fa723fc1b171.global-contribute.live-video.net:443/app", true,
	 "Kick: Creator Dashboard > Settings > Stream URL & Key (copy the URL too if it differs)"},
	{"youtube", "YouTube", "rtmps://a.rtmps.youtube.com:443/live2", false,
	 "YouTube: YouTube Studio > Go live > Stream > Stream key"},
	{"custom", "Custom", "", false, "Enter the server URL and stream key from your service."},
};

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
