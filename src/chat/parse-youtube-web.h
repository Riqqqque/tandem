// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - pure parsers for YouTube's public web pages and the web chat endpoint that
// youtube.com itself uses. No API key is involved. Never throw.
#pragma once

#include "parse-youtube.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::chat {

// What the user typed in the YouTube chat field.
struct YouTubeTarget {
	enum class Kind { Invalid, Video, Channel };
	Kind kind = Kind::Invalid;
	std::string video_id;     // Video
	std::string channel_path; // Channel: "@handle", "channel/UC...", "c/name" or "user/name"
	// A bare 11-character word is tried as a handle first, then as a video id.
	bool could_be_video = false;
	std::string label;        // for status texts: "@handle", a channel id or a video id
};

// Accepts "@handle", "handle", any youtube.com/@handle, /channel/UC..., /c/..., /user/... link,
// a bare channel id (UC...), or a video id or video link (watch, youtu.be, /live/, ...).
YouTubeTarget ParseYouTubeTarget(std::string_view input);

// https://www.youtube.com/<channel_path>/live
std::string YouTubeLivePageUrl(const YouTubeTarget &t);

// Scans a (possibly partial) youtube.com page for <link rel="canonical" href="...">.
// Returns nullopt when the tag isn't there (yet). Otherwise the video id when the canonical URL
// is a watch link (the channel is live or has a stream scheduled), or "" when it points
// elsewhere (the channel page: nothing live).
std::optional<std::string> FindCanonicalVideoId(std::string_view html);

// The live_chat popout page (youtube.com/live_chat?is_popout=1&v=<id>).
struct YouTubeWebChatInfo {
	bool parsed = false;          // ytInitialData was found and read
	bool has_chat = false;        // a live chat is open for the video
	std::string message;          // why there is no chat ("Chat is disabled for this live stream.")
	std::string client_version;   // INNERTUBE_CLIENT_VERSION
	std::string continuation;     // reloads the newest messages of the full "Live chat" view
	std::string channel_id;       // the streamer's channel id when the page names it
};
YouTubeWebChatInfo ParseYouTubeLiveChatHtml(std::string_view html);

// JSON body for youtubei/v1/live_chat/get_live_chat.
std::string BuildYouTubeGetLiveChatBody(const std::string &client_version, const std::string &continuation);

// One get_live_chat response.
struct YouTubeWebChatBatch {
	bool ok = false;     // the response was JSON with a liveChatContinuation
	bool ended = false;  // no continuation: the stream (and its chat) has ended
	std::vector<YouTubeChatItem> items;
};
YouTubeWebChatBatch ParseYouTubeGetLiveChat(std::string_view body, int64_t fallback_now_ms);

} // namespace tandem::chat
