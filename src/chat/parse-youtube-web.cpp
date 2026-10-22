// SPDX-License-Identifier: GPL-2.0-or-later
#include "parse-youtube-web.h"

#include "chat-util.h"

#include <json.hpp>

#include <cstdint>

namespace tandem::chat {

using json = nlohmann::json;

namespace {

const json &Null()
{
	static const json n;
	return n;
}

const json &Child(const json &j, const char *key)
{
	if (!j.is_object())
		return Null();
	auto it = j.find(key);
	return it == j.end() ? Null() : *it;
}

const json &Path(const json &j, std::initializer_list<const char *> keys)
{
	const json *cur = &j;
	for (const char *k : keys) {
		cur = &Child(*cur, k);
		if (cur->is_null())
			return Null();
	}
	return *cur;
}

std::string Str(const json &j, const char *key)
{
	const json &v = Child(j, key);
	return v.is_string() ? v.get<std::string>() : std::string();
}

bool IsIdChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool IsHandleChar(char c)
{
	return IsIdChar(c) || c == '.' || static_cast<unsigned char>(c) >= 0x80; // handles may be non-Latin
}

bool AllOf(std::string_view s, bool (*pred)(char))
{
	if (s.empty())
		return false;
	for (char c : s) {
		if (!pred(c))
			return false;
	}
	return true;
}

bool StartsWithNoCase(std::string_view s, std::string_view prefix)
{
	return s.size() >= prefix.size() && ToLowerAscii(s.substr(0, prefix.size())) == prefix;
}

// "simpleText" or the concatenated "runs[].text".
std::string TextOf(const json &t)
{
	if (!t.is_object())
		return {};
	std::string s = Str(t, "simpleText");
	if (!s.empty())
		return s;
	const json &runs = Child(t, "runs");
	if (runs.is_array()) {
		for (const auto &r : runs)
			s += Str(r, "text");
	}
	return s;
}

// Appends message runs as text, keeping custom (channel member) emoji as image spans.
void AppendRuns(const json &message, ChatMessage &m)
{
	const json &runs = Child(message, "runs");
	if (!runs.is_array()) {
		m.text += Str(message, "simpleText");
		return;
	}
	for (const auto &r : runs) {
		if (r.contains("text")) {
			m.text += Str(r, "text");
			continue;
		}
		const json &emoji = Child(r, "emoji");
		if (!emoji.is_object())
			continue;
		const json &shortcuts = Child(emoji, "shortcuts");
		std::string name = shortcuts.is_array() && !shortcuts.empty() && shortcuts[0].is_string()
					   ? shortcuts[0].get<std::string>()
					   : std::string();
		const json &custom = Child(emoji, "isCustomEmoji");
		if (custom.is_boolean() && custom.get<bool>()) {
			const json &thumbs = Path(emoji, {"image", "thumbnails"});
			std::string url;
			if (thumbs.is_array() && !thumbs.empty())
				url = Str(thumbs.back(), "url");
			if (url.rfind("//", 0) == 0)
				url = "https:" + url;
			if (name.empty())
				name = Str(emoji, "emojiId");
			EmoteSpan span;
			span.begin = m.text.size();
			m.text += name;
			span.end = m.text.size();
			span.name = name;
			span.url = url;
			span.source = "youtube";
			if (!url.empty() && span.end > span.begin)
				m.emotes.push_back(std::move(span));
		} else {
			// Standard emoji: emojiId is the emoji itself.
			std::string id = Str(emoji, "emojiId");
			m.text += id.empty() ? name : id;
		}
	}
}

void ReadAuthor(const json &r, ChatMessage &m)
{
	m.platform = Platform::YouTube;
	m.id = Str(r, "id");
	m.author = TextOf(Child(r, "authorName"));
	m.author_id = Str(r, "authorExternalChannelId");
	const json &badges = Child(r, "authorBadges");
	if (badges.is_array()) {
		for (const auto &b : badges) {
			const json &br = Child(b, "liveChatAuthorBadgeRenderer");
			std::string icon = Str(Child(br, "icon"), "iconType");
			if (icon == "OWNER")
				m.badges.emplace_back("owner");
			else if (icon == "MODERATOR")
				m.badges.emplace_back("moderator");
			else if (icon == "VERIFIED")
				m.badges.emplace_back("verified");
			else if (Child(br, "customThumbnail").is_object())
				m.badges.emplace_back("member");
		}
	}
}

std::optional<YouTubeChatItem> ReadChatItem(const json &item, int64_t fallback_now_ms)
{
	if (!item.is_object() || item.empty())
		return std::nullopt;
	const std::string type = item.begin().key();
	const json &r = item.begin().value();
	YouTubeChatItem out;
	ChatMessage &m = out.message;
	if (type == "liveChatTextMessageRenderer") {
		ReadAuthor(r, m);
		AppendRuns(Child(r, "message"), m);
	} else if (type == "liveChatPaidMessageRenderer") {
		ReadAuthor(r, m);
		m.text = "[Super Chat " + TextOf(Child(r, "purchaseAmountText")) + "]";
		if (Child(r, "message").is_object()) {
			m.text += " ";
			AppendRuns(Child(r, "message"), m);
		}
	} else if (type == "liveChatPaidStickerRenderer") {
		ReadAuthor(r, m);
		m.text = "[Super Sticker " + TextOf(Child(r, "purchaseAmountText")) + "]";
	} else if (type == "liveChatMembershipItemRenderer") {
		ReadAuthor(r, m);
		std::string header = TextOf(Child(r, "headerPrimaryText"));
		if (header.empty())
			header = TextOf(Child(r, "headerSubtext"));
		m.text = header.empty() ? "[New member]" : "[" + header + "]";
		if (Child(r, "message").is_object()) {
			m.text += " ";
			AppendRuns(Child(r, "message"), m);
		}
	} else if (type == "liveChatSponsorshipsGiftPurchaseAnnouncementRenderer") {
		const json &h = Path(r, {"header", "liveChatSponsorshipsHeaderRenderer"});
		ReadAuthor(h, m);
		m.id = Str(r, "id");
		m.author_id = Str(r, "authorExternalChannelId");
		std::string text = TextOf(Child(h, "primaryText"));
		m.text = text.empty() ? "[Gifted memberships]" : "[" + text + "]";
	} else {
		return std::nullopt; // engagement notices, placeholders, polls, gift receipts
	}
	if (m.id.empty())
		return std::nullopt;
	const std::string usec = Str(r, "timestampUsec");
	int64_t ts = 0;
	for (char c : usec) {
		if (c < '0' || c > '9' || ts > INT64_MAX / 10) {
			ts = 0;
			break;
		}
		ts = ts * 10 + (c - '0');
	}
	m.timestamp_ms = ts >= 1000 ? ts / 1000 : fallback_now_ms;
	return out;
}

// Copies the JSON object that starts at `start` ('{'), honoring strings and escapes.
std::optional<std::string_view> ObjectAt(std::string_view s, size_t start)
{
	if (start >= s.size() || s[start] != '{')
		return std::nullopt;
	int depth = 0;
	bool in_str = false, esc = false;
	for (size_t i = start; i < s.size(); ++i) {
		char c = s[i];
		if (in_str) {
			if (esc)
				esc = false;
			else if (c == '\\')
				esc = true;
			else if (c == '"')
				in_str = false;
			continue;
		}
		if (c == '"')
			in_str = true;
		else if (c == '{')
			++depth;
		else if (c == '}' && --depth == 0)
			return s.substr(start, i - start + 1);
	}
	return std::nullopt;
}

std::string QuotedAfter(std::string_view html, std::string_view key)
{
	size_t p = html.find(key);
	if (p == std::string_view::npos)
		return {};
	p += key.size();
	size_t e = html.find('"', p);
	if (e == std::string_view::npos || e - p > 4096)
		return {};
	return std::string(html.substr(p, e - p));
}

} // namespace

YouTubeTarget ParseYouTubeTarget(std::string_view input)
{
	YouTubeTarget t;
	std::string_view s = TrimAscii(input);
	if (s.empty())
		return t;

	// Video links first: watch?v=, youtu.be/, /live/<id>, /shorts/, /embed/.
	bool looks_like_url = s.find('/') != std::string_view::npos || s.find('?') != std::string_view::npos ||
			      s.find('.') != std::string_view::npos;
	if (looks_like_url) {
		if (auto id = ExtractYouTubeVideoId(s)) {
			t.kind = YouTubeTarget::Kind::Video;
			t.video_id = *id;
			t.label = *id;
			return t;
		}
	}

	// Strip scheme and host to get at the path of a channel link.
	std::string_view p = s;
	for (std::string_view scheme : {"https://", "http://"}) {
		if (StartsWithNoCase(p, scheme))
			p.remove_prefix(scheme.size());
	}
	for (std::string_view host : {"www.youtube.com", "m.youtube.com", "youtube.com"}) {
		if (StartsWithNoCase(p, host)) {
			p.remove_prefix(host.size());
			if (!p.empty() && p.front() == ':') { // port
				size_t slash = p.find('/');
				p.remove_prefix(slash == std::string_view::npos ? p.size() : slash);
			}
			if (!p.empty() && p.front() == '/')
				p.remove_prefix(1);
			break;
		}
	}
	size_t cut = p.find_first_of("?#");
	if (cut != std::string_view::npos)
		p = p.substr(0, cut);
	std::string_view first = p.substr(0, p.find('/'));
	std::string_view rest = first.size() < p.size() ? p.substr(first.size() + 1) : std::string_view();
	std::string_view second = rest.substr(0, rest.find('/'));

	if (!first.empty() && first.front() == '@') {
		std::string_view h = first.substr(1);
		if (AllOf(h, IsHandleChar) && h.size() <= 100) {
			t.kind = YouTubeTarget::Kind::Channel;
			t.channel_path = "@" + std::string(h);
			t.label = t.channel_path;
		}
		return t;
	}
	if (first == "channel") {
		if (IsValidYouTubeChannelId(second)) {
			t.kind = YouTubeTarget::Kind::Channel;
			t.channel_path = "channel/" + std::string(second);
			t.label = std::string(second);
		}
		return t;
	}
	if (first == "c" || first == "user") {
		if (AllOf(second, IsHandleChar) && second.size() <= 100) {
			t.kind = YouTubeTarget::Kind::Channel;
			t.channel_path = std::string(first) + "/" + std::string(second);
			t.label = std::string(second);
		}
		return t;
	}
	if (rest.empty() && IsValidYouTubeChannelId(first)) {
		t.kind = YouTubeTarget::Kind::Channel;
		t.channel_path = "channel/" + std::string(first);
		t.label = std::string(first);
		return t;
	}
	// A bare word is a handle typed without the '@'.
	if (rest.empty() && first.size() == s.size() && AllOf(first, IsHandleChar) && first.size() <= 100) {
		t.kind = YouTubeTarget::Kind::Channel;
		t.channel_path = "@" + std::string(first);
		t.label = t.channel_path;
		t.could_be_video = first.size() == 11 && AllOf(first, IsIdChar);
		if (t.could_be_video)
			t.video_id = std::string(first);
	}
	return t;
}

std::string YouTubeLivePageUrl(const YouTubeTarget &t)
{
	return "https://www.youtube.com/" + t.channel_path + "/live";
}

std::optional<std::string> FindCanonicalVideoId(std::string_view html)
{
	constexpr std::string_view kTag = "<link rel=\"canonical\" href=\"";
	size_t p = html.find(kTag);
	if (p == std::string_view::npos)
		return std::nullopt;
	p += kTag.size();
	size_t e = html.find('"', p);
	if (e == std::string_view::npos)
		return std::nullopt; // tag cut off at the end of a partial page
	std::string_view href = html.substr(p, e - p);
	constexpr std::string_view kWatch = "watch?v=";
	size_t w = href.find(kWatch);
	if (w == std::string_view::npos)
		return std::string();
	std::string_view id = href.substr(w + kWatch.size(), 11);
	if (id.size() != 11 || !AllOf(id, IsIdChar))
		return std::string();
	return std::string(id);
}

YouTubeWebChatInfo ParseYouTubeLiveChatHtml(std::string_view html)
{
	YouTubeWebChatInfo info;
	info.client_version = QuotedAfter(html, "\"INNERTUBE_CLIENT_VERSION\":\"");
	size_t p = std::string_view::npos;
	for (std::string_view marker : {"window[\"ytInitialData\"] = ", "var ytInitialData = ", "ytInitialData = "}) {
		p = html.find(marker);
		if (p != std::string_view::npos) {
			p += marker.size();
			break;
		}
	}
	if (p == std::string_view::npos)
		return info;
	auto obj = ObjectAt(html, p);
	if (!obj)
		return info;
	json d = json::parse(*obj, nullptr, false);
	if (d.is_discarded() || !d.is_object())
		return info;
	info.parsed = true;

	const json &contents = Child(d, "contents");
	const json &lc = Child(contents, "liveChatRenderer");
	if (!lc.is_object()) {
		info.message = TextOf(Path(contents, {"messageRenderer", "text"}));
		return info;
	}
	// "Live chat" (every message) rather than the default "Top chat" (filtered).
	const json &items =
		Path(lc, {"header", "liveChatHeaderRenderer", "viewSelector", "sortFilterSubMenuRenderer", "subMenuItems"});
	if (items.is_array() && !items.empty()) {
		const json &item = items.size() >= 2 ? items[1] : items[0];
		info.continuation = Str(Path(item, {"continuation", "reloadContinuationData"}), "continuation");
	}
	if (info.continuation.empty()) {
		const json &conts = Child(lc, "continuations");
		if (conts.is_array() && !conts.empty() && conts[0].is_object() && !conts[0].empty())
			info.continuation = Str(conts[0].begin().value(), "continuation");
	}
	info.has_chat = !info.continuation.empty() && !info.client_version.empty();
	return info;
}

std::string BuildYouTubeGetLiveChatBody(const std::string &client_version, const std::string &continuation)
{
	json body = {
		{"context", {{"client", {{"clientName", "WEB"}, {"clientVersion", client_version}, {"hl", "en"}}}}},
		{"continuation", continuation},
	};
	return body.dump();
}

YouTubeWebChatBatch ParseYouTubeGetLiveChat(std::string_view body, int64_t fallback_now_ms)
{
	YouTubeWebChatBatch batch;
	json d = json::parse(body, nullptr, false);
	if (d.is_discarded() || !d.is_object())
		return batch;
	const json &lcc = Path(d, {"continuationContents", "liveChatContinuation"});
	if (!lcc.is_object()) {
		// A valid response without a chat: the stream is over.
		batch.ok = d.contains("responseContext");
		batch.ended = batch.ok;
		return batch;
	}
	batch.ok = true;
	const json &conts = Child(lcc, "continuations");
	batch.ended = !conts.is_array() || conts.empty();

	const json &actions = Child(lcc, "actions");
	if (!actions.is_array())
		return batch;
	for (const auto &a : actions) {
		if (const json &add = Child(a, "addChatItemAction"); add.is_object()) {
			if (auto it = ReadChatItem(Child(add, "item"), fallback_now_ms))
				batch.items.push_back(std::move(*it));
			continue;
		}
		std::string target = Str(Child(a, "removeChatItemAction"), "targetItemId");
		if (target.empty())
			target = Str(Child(a, "markChatItemAsDeletedAction"), "targetItemId");
		if (!target.empty()) {
			YouTubeChatItem it;
			it.kind = YouTubeChatItem::Kind::Delete;
			it.target_id = target;
			batch.items.push_back(std::move(it));
			continue;
		}
		std::string author = Str(Child(a, "removeChatItemByAuthorAction"), "externalChannelId");
		if (author.empty())
			author = Str(Child(a, "markChatItemsByAuthorAsDeletedAction"), "externalChannelId");
		if (!author.empty()) {
			YouTubeChatItem it;
			it.kind = YouTubeChatItem::Kind::ClearUser;
			it.target_id = author;
			batch.items.push_back(std::move(it));
		}
	}
	return batch;
}

} // namespace tandem::chat
