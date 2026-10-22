// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - finding a channel's live stream and its chat through youtube.com's public pages.
// No API key and no quota. Used by both YouTube providers.
#pragma once

#include "net.h"
#include "parse-youtube-web.h"

#include <atomic>
#include <string>

namespace tandem::chat {

// Headers for youtube.com pages: the chat page only works for browsers, and the consent cookie
// skips the EU cookie screen. No account cookies are ever sent.
const HttpHeaders &YouTubeWebHeaders();

struct YouTubeLiveLookup {
	enum class Result { Live, NotLive, NotFound, Error };
	Result result = Result::Error;
	std::string video_id;   // Live
	std::string channel_id; // when the page names it
	std::string error;      // Error: short reason
	long retry_after_s = 0;
};

// Reads the channel's /live page until its canonical link shows whether a stream is on.
YouTubeLiveLookup LookUpYouTubeLive(const YouTubeTarget &target, const std::atomic<bool> *cancel);

// Loads the live chat popout page of a video.
struct YouTubeChatPageResult {
	bool fetched = false;   // got an HTTP 200 page
	std::string error;      // transport or HTTP error
	long retry_after_s = 0;
	YouTubeWebChatInfo info;
};
YouTubeChatPageResult FetchYouTubeChatPage(const std::string &video_id, const std::atomic<bool> *cancel);

} // namespace tandem::chat
