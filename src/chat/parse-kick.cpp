// SPDX-License-Identifier: GPL-2.0-or-later
#include "parse-kick.h"

#include "chat-util.h"

#include <json.hpp>

namespace tandem::chat {

using json = nlohmann::json;

namespace {

// Kick's Pusher event names (unofficial, may change).
constexpr std::string_view kEvChatMessage = "App\\Events\\ChatMessageEvent";
constexpr std::string_view kEvMessageDeleted = "App\\Events\\MessageDeletedEvent";
constexpr std::string_view kEvUserBanned = "App\\Events\\UserBannedEvent";
constexpr std::string_view kEvChatroomClear = "App\\Events\\ChatroomClearEvent";

// Pusher wraps payloads as a JSON-encoded string; tolerate a plain object too.
json DecodeData(const json &env)
{
	auto it = env.find("data");
	if (it == env.end())
		return json();
	if (it->is_string())
		return json::parse(it->get<std::string>(), nullptr, false);
	return *it;
}

std::string IdString(const json &j)
{
	if (j.is_string())
		return j.get<std::string>();
	if (j.is_number_unsigned())
		return std::to_string(j.get<uint64_t>());
	if (j.is_number_integer())
		return std::to_string(j.get<int64_t>());
	if (j.is_number_float())
		return std::to_string(static_cast<int64_t>(j.get<double>()));
	return {};
}

std::string StrOr(const json &obj, const char *key)
{
	if (!obj.is_object())
		return {};
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_string())
		return {};
	return it->get<std::string>();
}

const json &Child(const json &obj, const char *key)
{
	static const json kNull;
	if (!obj.is_object())
		return kNull;
	auto it = obj.find(key);
	return it == obj.end() ? kNull : *it;
}

void AddBadge(std::vector<std::string> &out, const char *name)
{
	for (const auto &b : out) {
		if (b == name)
			return;
	}
	out.emplace_back(name);
}

std::vector<std::string> MapKickBadges(const json &badges)
{
	std::vector<std::string> out;
	if (!badges.is_array())
		return out;
	for (const auto &b : badges) {
		std::string t = ToLowerAscii(StrOr(b, "type"));
		if (t == "broadcaster")
			AddBadge(out, "broadcaster");
		else if (t == "moderator")
			AddBadge(out, "moderator");
		else if (t == "subscriber" || t == "founder")
			AddBadge(out, "subscriber");
		else if (t == "vip")
			AddBadge(out, "vip");
		else if (t == "verified")
			AddBadge(out, "verified");
	}
	return out;
}

KickEvent Malformed(KickEvent ev, const char *why)
{
	ev.type = KickEvent::Type::Malformed;
	ev.error_text = why;
	return ev;
}

} // namespace

std::string StripKickEmotes(std::string_view text)
{
	constexpr std::string_view kOpen = "[emote:";
	std::string out;
	out.reserve(text.size());
	size_t p = 0;
	while (p < text.size()) {
		size_t start = text.find(kOpen, p);
		if (start == std::string_view::npos) {
			out.append(text.substr(p));
			break;
		}
		out.append(text.substr(p, start - p));
		size_t q = start + kOpen.size();
		size_t digits = q;
		while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9')
			++digits;
		size_t close = text.find(']', digits);
		if (digits == q || digits >= text.size() || text[digits] != ':' || close == std::string_view::npos ||
		    close == digits + 1 || text.substr(digits + 1, close - digits - 1).find('[') != std::string_view::npos) {
			out.append(kOpen);
			p = q;
			continue;
		}
		out.append(text.substr(digits + 1, close - digits - 1));
		p = close + 1;
	}
	return out;
}

bool IsValidKickSlug(std::string_view slug)
{
	if (slug.empty() || slug.size() > 64)
		return false;
	for (char c : slug) {
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
		      c == '-'))
			return false;
	}
	return true;
}

bool IsValidKickChatroomId(std::string_view id)
{
	if (id.empty() || id.size() > 19 || id.front() == '0')
		return false;
	for (char c : id) {
		if (c < '0' || c > '9')
			return false;
	}
	return true;
}

std::optional<std::string> ParseKickChannelChatroomId(std::string_view body)
{
	json j = json::parse(body, nullptr, false);
	if (j.is_discarded())
		return std::nullopt;
	std::string id = IdString(Child(Child(j, "chatroom"), "id"));
	if (!IsValidKickChatroomId(id))
		return std::nullopt;
	return id;
}

std::string KickSubscribeMessage(const std::string &chatroom_id)
{
	json j = {{"event", "pusher:subscribe"},
		  {"data", {{"auth", ""}, {"channel", "chatrooms." + chatroom_id + ".v2"}}}};
	return j.dump();
}

std::string KickPingMessage()
{
	return R"({"event":"pusher:ping","data":{}})";
}

std::string KickPongMessage()
{
	return R"({"event":"pusher:pong","data":{}})";
}

KickEvent ParseKickPusher(std::string_view text, int64_t fallback_now_ms)
{
	KickEvent ev;
	try {
		json env = json::parse(text, nullptr, false);
		if (env.is_discarded() || !env.is_object())
			return Malformed(ev, "not a JSON object");
		ev.event_name = StrOr(env, "event");
		ev.channel = StrOr(env, "channel");
		const std::string &name = ev.event_name;
		if (name.empty())
			return Malformed(ev, "missing event name");

		if (name == "pusher:connection_established") {
			json d = DecodeData(env);
			ev.type = KickEvent::Type::ConnectionEstablished;
			const json &t = Child(d, "activity_timeout");
			if (t.is_number_integer() && t.get<int64_t>() > 0 && t.get<int64_t>() < 3600)
				ev.activity_timeout_s = static_cast<int>(t.get<int64_t>());
			return ev;
		}
		if (name == "pusher_internal:subscription_succeeded") {
			ev.type = KickEvent::Type::SubscriptionSucceeded;
			return ev;
		}
		if (name == "pusher:ping") {
			ev.type = KickEvent::Type::Ping;
			return ev;
		}
		if (name == "pusher:pong") {
			ev.type = KickEvent::Type::Pong;
			return ev;
		}
		if (name == "pusher:error") {
			ev.type = KickEvent::Type::PusherError;
			json d = DecodeData(env);
			const json &code = Child(d, "code");
			if (code.is_number_integer())
				ev.error_code = static_cast<int>(code.get<int64_t>());
			ev.error_text = StrOr(d, "message");
			return ev;
		}
		if (name == kEvChatMessage) {
			json d = DecodeData(env);
			if (!d.is_object())
				return Malformed(ev, "payload is not an object");
			const json &sender = Child(d, "sender");
			ChatMessage &m = ev.message;
			m.platform = Platform::Kick;
			m.id = IdString(Child(d, "id"));
			m.author_id = IdString(Child(sender, "id"));
			m.author = StrOr(sender, "username");
			if (m.author.empty())
				m.author = StrOr(sender, "slug");
			const json &identity = Child(sender, "identity");
			m.color = StrOr(identity, "color");
			m.badges = MapKickBadges(Child(identity, "badges"));
			const json &content = Child(d, "content");
			if (!content.is_string())
				return Malformed(ev, "missing content");
			m.text = StripKickEmotes(content.get<std::string>());
			auto ts = ParseIso8601Ms(StrOr(d, "created_at"));
			m.timestamp_ms = ts ? *ts : fallback_now_ms;
			ev.type = KickEvent::Type::Message;
			return ev;
		}
		if (name == kEvMessageDeleted) {
			json d = DecodeData(env);
			ev.target_id = IdString(Child(Child(d, "message"), "id"));
			if (ev.target_id.empty())
				return Malformed(ev, "missing message.id");
			ev.type = KickEvent::Type::DeleteMessage;
			return ev;
		}
		if (name == kEvUserBanned) {
			json d = DecodeData(env);
			ev.target_id = IdString(Child(Child(d, "user"), "id"));
			if (ev.target_id.empty())
				return Malformed(ev, "missing user.id");
			ev.type = KickEvent::Type::ClearUser;
			return ev;
		}
		if (name == kEvChatroomClear) {
			ev.type = KickEvent::Type::ClearAll;
			return ev;
		}
		ev.type = KickEvent::Type::Unknown;
		return ev;
	} catch (const std::exception &e) {
		ev.message = ChatMessage{};
		return Malformed(ev, e.what());
	}
}

} // namespace tandem::chat
