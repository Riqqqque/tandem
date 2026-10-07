// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - pure parsers for YouTube Data API v3 live chat responses. Never throw.
#pragma once

#include "chat-types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::chat {

// Accepts an 11-character video id or a youtube.com/watch?v=, youtu.be/, /live/, /shorts/,
// /embed/ URL (any scheme, www./m./music. hosts, extra query parameters).
std::optional<std::string> ExtractYouTubeVideoId(std::string_view input);

// "UC" followed by 22 characters of [A-Za-z0-9_-].
bool IsValidYouTubeChannelId(std::string_view id);

// Percent-encodes everything except RFC 3986 unreserved characters.
std::string UrlEncode(std::string_view s);

// Splits a server-streamed JSON array ("[{...},{...}") into its top-level objects as they
// complete. Tracks brace depth and string/escape state, so braces inside strings are safe.
class JsonObjectStreamSplitter {
public:
	explicit JsonObjectStreamSplitter(size_t max_object_bytes = 16 * 1024 * 1024) : max_(max_object_bytes) {}

	void Feed(std::string_view chunk, const std::function<void(std::string &&)> &on_object);
	bool overflow() const { return overflow_; }
	bool mid_object() const { return depth_ > 0; }
	void Reset();

private:
	size_t max_;
	std::string cur_;
	int depth_ = 0;
	bool in_string_ = false;
	bool escape_ = false;
	bool overflow_ = false;
};

struct YouTubeApiError {
	long http_status = 0;
	std::vector<std::string> reasons; // errors[].reason and details[].reason
	std::string message;
};

YouTubeApiError ParseYouTubeError(long http_status, std::string_view body);

enum class YouTubeErrorClass {
	QuotaExhausted,
	InvalidKey,
	KeyRestricted,
	ApiNotEnabled,
	ChatEnded,     // liveChatEnded / liveChatNotFound
	ChatDisabled,
	Forbidden,     // e.g. members-only chat
	RateLimited,   // transient: back off
	Transient,     // 5xx / no response
	NotFound,      // 404 without a specific reason
	Other,
};

YouTubeErrorClass ClassifyYouTubeError(const YouTubeApiError &e);
// True when retrying cannot help.
bool IsFatalYouTubeError(YouTubeErrorClass c);
// User-facing text; never contains the key.
std::string YouTubeErrorText(YouTubeErrorClass c, const YouTubeApiError &e);

struct YouTubeVideoInfo {
	bool found = false;            // items[] had the video
	bool is_live_broadcast = false; // liveStreamingDetails present
	bool ended = false;            // actualEndTime present
	std::string active_chat_id;
};
std::optional<YouTubeVideoInfo> ParseYouTubeVideosList(std::string_view body);

// search.list?eventType=live: the first live video id, "" when none, nullopt when malformed.
std::optional<std::string> ParseYouTubeSearchLive(std::string_view body);

struct YouTubeChatItem {
	enum class Kind { Message, Delete, ClearUser, ChatEnded };
	Kind kind = Kind::Message;
	ChatMessage message;
	std::string target_id;
};

struct YouTubeChatPage {
	bool ok = false;          // parsed as a LiveChatMessageListResponse
	bool has_error = false;   // the object was an {"error": ...} body
	YouTubeApiError error;
	std::vector<YouTubeChatItem> items;
	std::string next_page_token;
	int64_t polling_interval_ms = 0;
	bool offline = false;     // offlineAt present
};

YouTubeChatPage ParseYouTubeChatPage(std::string_view body, int64_t fallback_now_ms);

} // namespace tandem::chat
