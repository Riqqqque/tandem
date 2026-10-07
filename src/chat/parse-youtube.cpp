// SPDX-License-Identifier: GPL-2.0-or-later
#include "parse-youtube.h"

#include "chat-util.h"

#include <json.hpp>

namespace tandem::chat {

using json = nlohmann::json;

namespace {

bool IsIdChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool IsVideoId(std::string_view s)
{
	if (s.size() != 11)
		return false;
	for (char c : s) {
		if (!IsIdChar(c))
			return false;
	}
	return true;
}

bool StartsWithNoCase(std::string_view s, std::string_view prefix)
{
	return s.size() >= prefix.size() && ToLowerAscii(s.substr(0, prefix.size())) == prefix;
}

// First path segment after `prefix` ("/live/"), cut at '/', '?', '#', '&'.
std::optional<std::string> Segment(std::string_view path, std::string_view prefix)
{
	if (!StartsWithNoCase(path, prefix))
		return std::nullopt;
	path.remove_prefix(prefix.size());
	size_t end = path.find_first_of("/?#&");
	std::string_view id = path.substr(0, end);
	if (!IsVideoId(id))
		return std::nullopt;
	return std::string(id);
}

std::optional<std::string> QueryParam(std::string_view query, std::string_view key)
{
	while (!query.empty()) {
		size_t amp = query.find('&');
		std::string_view kv = query.substr(0, amp);
		size_t eq = kv.find('=');
		if (eq != std::string_view::npos && kv.substr(0, eq) == key)
			return std::string(kv.substr(eq + 1));
		if (amp == std::string_view::npos)
			break;
		query.remove_prefix(amp + 1);
	}
	return std::nullopt;
}

const json &Child(const json &obj, const char *key)
{
	static const json kNull;
	if (!obj.is_object())
		return kNull;
	auto it = obj.find(key);
	return it == obj.end() ? kNull : *it;
}

std::string Str(const json &obj, const char *key)
{
	const json &v = Child(obj, key);
	return v.is_string() ? v.get<std::string>() : std::string();
}

bool Bool(const json &obj, const char *key)
{
	const json &v = Child(obj, key);
	return v.is_boolean() && v.get<bool>();
}

int64_t Int(const json &obj, const char *key)
{
	const json &v = Child(obj, key);
	if (v.is_number_integer())
		return v.get<int64_t>();
	if (v.is_number_float())
		return static_cast<int64_t>(v.get<double>());
	if (v.is_string()) {
		try {
			return std::stoll(v.get<std::string>());
		} catch (...) {
		}
	}
	return 0;
}

bool HasReason(const YouTubeApiError &e, std::string_view r)
{
	for (const auto &x : e.reasons) {
		if (x == r)
			return true;
	}
	return false;
}

} // namespace

std::optional<std::string> ExtractYouTubeVideoId(std::string_view input)
{
	std::string_view s = TrimAscii(input);
	if (IsVideoId(s))
		return std::string(s);

	if (StartsWithNoCase(s, "https://"))
		s.remove_prefix(8);
	else if (StartsWithNoCase(s, "http://"))
		s.remove_prefix(7);
	else if (StartsWithNoCase(s, "//"))
		s.remove_prefix(2);

	size_t host_end = s.find_first_of("/?#");
	std::string host = ToLowerAscii(s.substr(0, host_end));
	std::string_view rest = host_end == std::string_view::npos ? std::string_view() : s.substr(host_end);
	// Drop a ":port".
	if (size_t colon = host.find(':'); colon != std::string::npos)
		host.resize(colon);
	for (const char *sub : {"www.", "m.", "music.", "gaming."}) {
		std::string_view p(sub);
		if (host.size() > p.size() && host.compare(0, p.size(), p) == 0) {
			host.erase(0, p.size());
			break;
		}
	}

	if (host == "youtu.be")
		return Segment(rest, "/");
	if (host != "youtube.com" && host != "youtube-nocookie.com")
		return std::nullopt;

	std::string_view path = rest.substr(0, rest.find_first_of("?#"));
	std::string_view query;
	if (size_t q = rest.find('?'); q != std::string_view::npos) {
		query = rest.substr(q + 1);
		query = query.substr(0, query.find('#'));
	}
	if (path == "/watch" || path == "/watch/") {
		auto v = QueryParam(query, "v");
		if (v && IsVideoId(*v))
			return v;
		return std::nullopt;
	}
	for (const char *prefix : {"/live/", "/shorts/", "/embed/", "/v/", "/e/"}) {
		if (auto id = Segment(path, prefix))
			return id;
	}
	return std::nullopt;
}

bool IsValidYouTubeChannelId(std::string_view id)
{
	if (id.size() != 24 || id.substr(0, 2) != "UC")
		return false;
	for (char c : id) {
		if (!IsIdChar(c))
			return false;
	}
	return true;
}

std::string UrlEncode(std::string_view s)
{
	static const char kHex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(s.size() * 3);
	for (unsigned char c : s) {
		if (IsIdChar(static_cast<char>(c)) || c == '.' || c == '~') {
			out.push_back(static_cast<char>(c));
		} else {
			out.push_back('%');
			out.push_back(kHex[c >> 4]);
			out.push_back(kHex[c & 15]);
		}
	}
	return out;
}

void JsonObjectStreamSplitter::Reset()
{
	cur_.clear();
	depth_ = 0;
	in_string_ = false;
	escape_ = false;
	overflow_ = false;
}

void JsonObjectStreamSplitter::Feed(std::string_view chunk, const std::function<void(std::string &&)> &on_object)
{
	size_t start = 0; // start of the pending slice of `chunk` belonging to the current object
	for (size_t i = 0; i < chunk.size(); ++i) {
		char c = chunk[i];
		if (depth_ == 0) {
			// Between objects: skip '[', ',', ']' and whitespace until the next object opens.
			if (c == '{') {
				depth_ = 1;
				in_string_ = false;
				escape_ = false;
				cur_.clear();
				start = i;
			}
			continue;
		}
		if (in_string_) {
			if (escape_)
				escape_ = false;
			else if (c == '\\')
				escape_ = true;
			else if (c == '"')
				in_string_ = false;
			continue;
		}
		if (c == '"') {
			in_string_ = true;
		} else if (c == '{' || c == '[') {
			++depth_;
		} else if (c == '}' || c == ']') {
			if (--depth_ == 0) {
				const size_t piece = i + 1 - start;
				if (!overflow_ && cur_.size() + piece <= max_) {
					cur_.append(chunk.substr(start, piece));
					std::string obj = std::move(cur_);
					cur_.clear();
					on_object(std::move(obj));
				}
				cur_.clear();
				overflow_ = false;
			}
		}
	}
	if (depth_ > 0) {
		std::string_view tail = chunk.substr(start);
		if (cur_.size() + tail.size() > max_) {
			// Too large: drop what we have but keep tracking structure so we resync.
			overflow_ = true;
			cur_.clear();
		} else if (!overflow_) {
			cur_.append(tail);
		}
	}
}

YouTubeApiError ParseYouTubeError(long http_status, std::string_view body)
{
	YouTubeApiError e;
	e.http_status = http_status;
	json j = json::parse(body, nullptr, false);
	if (j.is_array() && !j.empty())
		j = j[0]; // streamed error bodies can arrive wrapped in an array
	const json &err = Child(j, "error");
	if (!err.is_object())
		return e;
	e.message = Str(err, "message");
	const json &errors = Child(err, "errors");
	if (errors.is_array()) {
		for (const auto &x : errors) {
			std::string r = Str(x, "reason");
			if (!r.empty())
				e.reasons.push_back(r);
		}
	}
	const json &details = Child(err, "details");
	if (details.is_array()) {
		for (const auto &x : details) {
			std::string r = Str(x, "reason");
			if (!r.empty())
				e.reasons.push_back(r);
		}
	}
	if (e.http_status == 0)
		e.http_status = static_cast<long>(Int(err, "code"));
	return e;
}

YouTubeErrorClass ClassifyYouTubeError(const YouTubeApiError &e)
{
	using C = YouTubeErrorClass;
	if (HasReason(e, "quotaExceeded") || HasReason(e, "dailyLimitExceeded"))
		return C::QuotaExhausted;
	if (HasReason(e, "keyInvalid") || HasReason(e, "API_KEY_INVALID") || HasReason(e, "keyExpired") ||
	    HasReason(e, "API_KEY_EXPIRED") || e.message.find("API key not valid") != std::string::npos ||
	    e.message.find("API key expired") != std::string::npos)
		return C::InvalidKey;
	if (HasReason(e, "API_KEY_SERVICE_BLOCKED") || HasReason(e, "API_KEY_HTTP_REFERRER_BLOCKED") ||
	    HasReason(e, "API_KEY_IP_ADDRESS_BLOCKED") || HasReason(e, "API_KEY_ANDROID_APP_BLOCKED") ||
	    HasReason(e, "API_KEY_IOS_APP_BLOCKED") || HasReason(e, "ipRefererBlocked"))
		return C::KeyRestricted;
	if (HasReason(e, "accessNotConfigured") || HasReason(e, "SERVICE_DISABLED"))
		return C::ApiNotEnabled;
	if (HasReason(e, "liveChatEnded") || HasReason(e, "liveChatNotFound"))
		return C::ChatEnded;
	if (HasReason(e, "liveChatDisabled"))
		return C::ChatDisabled;
	if (HasReason(e, "rateLimitExceeded") || HasReason(e, "userRateLimitExceeded") ||
	    HasReason(e, "RATE_LIMIT_EXCEEDED") || e.http_status == 429)
		return C::RateLimited;
	if (e.http_status == 0 || e.http_status >= 500)
		return C::Transient;
	if (e.http_status == 403 && (HasReason(e, "forbidden") || HasReason(e, "insufficientPermissions")))
		return C::Forbidden;
	if (e.http_status == 404)
		return C::NotFound;
	return C::Other;
}

bool IsFatalYouTubeError(YouTubeErrorClass c)
{
	using C = YouTubeErrorClass;
	switch (c) {
	case C::QuotaExhausted:
	case C::InvalidKey:
	case C::KeyRestricted:
	case C::ApiNotEnabled:
	case C::ChatEnded:
	case C::ChatDisabled:
	case C::Forbidden:
		return true;
	case C::RateLimited:
	case C::Transient:
	case C::NotFound:
	case C::Other:
		return false;
	}
	return false;
}

std::string YouTubeErrorText(YouTubeErrorClass c, const YouTubeApiError &e)
{
	using C = YouTubeErrorClass;
	switch (c) {
	case C::QuotaExhausted:
		return "YouTube API quota exhausted for today";
	case C::InvalidKey:
		return "YouTube API key is not valid";
	case C::KeyRestricted:
		return "YouTube API key is restricted and cannot call the YouTube Data API";
	case C::ApiNotEnabled:
		return "YouTube Data API v3 is not enabled for this key's Google Cloud project";
	case C::ChatEnded:
		return "Live chat has ended";
	case C::ChatDisabled:
		return "Live chat is disabled for this stream";
	case C::Forbidden:
		return "YouTube refused access to this live chat (for example a members-only chat)";
	case C::RateLimited:
		return "YouTube API rate limit reached";
	case C::Transient:
		return e.http_status ? "YouTube API unavailable (HTTP " + std::to_string(e.http_status) + ")"
				     : "YouTube API unreachable";
	case C::NotFound:
		return "Live chat has ended";
	case C::Other:
		break;
	}
	std::string s = "YouTube API error (HTTP " + std::to_string(e.http_status) + ")";
	if (!e.message.empty())
		s += ": " + e.message.substr(0, 200);
	return s;
}

std::optional<YouTubeVideoInfo> ParseYouTubeVideosList(std::string_view body)
{
	json j = json::parse(body, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return std::nullopt;
	YouTubeVideoInfo info;
	const json &items = Child(j, "items");
	if (!items.is_array() || items.empty())
		return info;
	info.found = true;
	const json &live = Child(items[0], "liveStreamingDetails");
	if (!live.is_object())
		return info;
	info.is_live_broadcast = true;
	info.ended = !Str(live, "actualEndTime").empty();
	info.active_chat_id = Str(live, "activeLiveChatId");
	return info;
}

std::optional<std::string> ParseYouTubeSearchLive(std::string_view body)
{
	json j = json::parse(body, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return std::nullopt;
	const json &items = Child(j, "items");
	if (!items.is_array())
		return std::string();
	for (const auto &it : items) {
		std::string id = Str(Child(it, "id"), "videoId");
		if (IsVideoId(id))
			return id;
	}
	return std::string();
}

namespace {

std::optional<YouTubeChatItem> ParseItem(const json &item, int64_t fallback_now_ms)
{
	const json &snip = Child(item, "snippet");
	const json &author = Child(item, "authorDetails");
	std::string type = Str(snip, "type");
	YouTubeChatItem out;

	if (type == "messageDeletedEvent") {
		out.kind = YouTubeChatItem::Kind::Delete;
		out.target_id = Str(Child(snip, "messageDeletedDetails"), "deletedMessageId");
		if (out.target_id.empty())
			return std::nullopt;
		return out;
	}
	if (type == "userBannedEvent") {
		out.kind = YouTubeChatItem::Kind::ClearUser;
		out.target_id = Str(Child(Child(snip, "userBannedDetails"), "bannedUserDetails"), "channelId");
		if (out.target_id.empty())
			return std::nullopt;
		return out;
	}
	if (type == "chatEndedEvent") {
		out.kind = YouTubeChatItem::Kind::ChatEnded;
		return out;
	}

	std::string display = Str(snip, "displayMessage");
	std::string text;
	if (type == "textMessageEvent") {
		text = display;
		if (text.empty())
			text = Str(Child(snip, "textMessageDetails"), "messageText");
	} else if (type == "superChatEvent") {
		const json &d = Child(snip, "superChatDetails");
		std::string comment = Str(d, "userComment");
		text = "[Super Chat " + Str(d, "amountDisplayString") + "]";
		if (!comment.empty())
			text += " " + comment;
	} else if (type == "superStickerEvent") {
		const json &d = Child(snip, "superStickerDetails");
		std::string alt = Str(Child(d, "superStickerMetadata"), "altText");
		text = "[Super Sticker " + Str(d, "amountDisplayString") + "]";
		if (!alt.empty())
			text += " " + alt;
	} else if (type == "newSponsorEvent") {
		std::string level = Str(Child(snip, "newSponsorDetails"), "memberLevelName");
		text = level.empty() ? "[New member]" : "[New member: " + level + "]";
	} else if (type == "memberMilestoneChatEvent") {
		const json &d = Child(snip, "memberMilestoneChatDetails");
		int64_t months = Int(d, "memberMonth");
		std::string comment = Str(d, "userComment");
		text = months > 0 ? "[Member for " + std::to_string(months) + " months]" : "[Member milestone]";
		if (!comment.empty())
			text += " " + comment;
	} else if (type == "membershipGiftingEvent") {
		int64_t n = Int(Child(snip, "membershipGiftingDetails"), "giftMembershipsCount");
		text = n > 0 ? "[Gifted " + std::to_string(n) + " memberships]" : "[Gifted memberships]";
	} else {
		return std::nullopt; // polls, gift receipts, tombstones, unknown types
	}

	ChatMessage &m = out.message;
	m.platform = Platform::YouTube;
	m.id = Str(item, "id");
	m.author_id = Str(author, "channelId");
	if (m.author_id.empty())
		m.author_id = Str(snip, "authorChannelId");
	m.author = Str(author, "displayName");
	if (Bool(author, "isChatOwner"))
		m.badges.emplace_back("owner");
	if (Bool(author, "isChatModerator"))
		m.badges.emplace_back("moderator");
	if (Bool(author, "isChatSponsor"))
		m.badges.emplace_back("member");
	if (Bool(author, "isVerified"))
		m.badges.emplace_back("verified");
	m.text = std::move(text);
	auto ts = ParseIso8601Ms(Str(snip, "publishedAt"));
	m.timestamp_ms = ts ? *ts : fallback_now_ms;
	return out;
}

} // namespace

YouTubeChatPage ParseYouTubeChatPage(std::string_view body, int64_t fallback_now_ms)
{
	YouTubeChatPage page;
	json j = json::parse(body, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return page;
	if (Child(j, "error").is_object()) {
		page.has_error = true;
		page.error = ParseYouTubeError(0, body);
		return page;
	}
	page.ok = true;
	page.next_page_token = Str(j, "nextPageToken");
	page.polling_interval_ms = Int(j, "pollingIntervalMillis");
	page.offline = !Str(j, "offlineAt").empty();
	const json &items = Child(j, "items");
	if (items.is_array()) {
		for (const auto &it : items) {
			try {
				if (auto x = ParseItem(it, fallback_now_ms))
					page.items.push_back(std::move(*x));
			} catch (const std::exception &) {
				// skip items with an unexpected shape
			}
		}
	}
	return page;
}

} // namespace tandem::chat
