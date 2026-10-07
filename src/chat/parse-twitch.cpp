// SPDX-License-Identifier: GPL-2.0-or-later
#include "parse-twitch.h"

#include "chat-util.h"

#include <charconv>

namespace tandem::chat {

const std::string *IrcLine::Tag(std::string_view key) const
{
	for (const auto &[k, v] : tags) {
		if (k == key)
			return &v;
	}
	return nullptr;
}

std::string UnescapeIrcTagValue(std::string_view v)
{
	std::string out;
	out.reserve(v.size());
	for (size_t i = 0; i < v.size(); ++i) {
		char c = v[i];
		if (c != '\\') {
			out.push_back(c);
			continue;
		}
		if (i + 1 >= v.size())
			break; // a lone trailing backslash is dropped
		char n = v[++i];
		switch (n) {
		case 's':
			out.push_back(' ');
			break;
		case ':':
			out.push_back(';');
			break;
		case '\\':
			out.push_back('\\');
			break;
		case 'r':
			out.push_back('\r');
			break;
		case 'n':
			out.push_back('\n');
			break;
		default:
			out.push_back(n);
			break;
		}
	}
	return out;
}

std::optional<IrcLine> ParseIrcLine(std::string_view line)
{
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
		line.remove_suffix(1);
	IrcLine out;
	size_t p = 0;
	auto skip_spaces = [&] {
		while (p < line.size() && line[p] == ' ')
			++p;
	};

	if (p < line.size() && line[p] == '@') {
		size_t end = line.find(' ', p);
		if (end == std::string_view::npos)
			return std::nullopt;
		std::string_view tags = line.substr(p + 1, end - p - 1);
		while (!tags.empty()) {
			size_t semi = tags.find(';');
			std::string_view item = tags.substr(0, semi);
			if (!item.empty()) {
				size_t eq = item.find('=');
				if (eq == std::string_view::npos)
					out.tags.emplace_back(std::string(item), std::string());
				else
					out.tags.emplace_back(std::string(item.substr(0, eq)),
							      UnescapeIrcTagValue(item.substr(eq + 1)));
			}
			if (semi == std::string_view::npos)
				break;
			tags.remove_prefix(semi + 1);
		}
		p = end;
		skip_spaces();
	}

	if (p < line.size() && line[p] == ':') {
		size_t end = line.find(' ', p);
		if (end == std::string_view::npos)
			return std::nullopt;
		out.prefix = std::string(line.substr(p + 1, end - p - 1));
		size_t bang = out.prefix.find('!');
		if (bang != std::string::npos)
			out.nick = out.prefix.substr(0, bang);
		else if (out.prefix.find('.') == std::string::npos)
			out.nick = out.prefix;
		p = end;
		skip_spaces();
	}

	size_t cmd_end = line.find(' ', p);
	out.command = std::string(line.substr(p, cmd_end == std::string_view::npos ? std::string_view::npos : cmd_end - p));
	if (out.command.empty())
		return std::nullopt;
	if (cmd_end == std::string_view::npos)
		return out;
	p = cmd_end;

	while (p < line.size()) {
		skip_spaces();
		if (p >= line.size())
			break;
		if (line[p] == ':') {
			out.params.emplace_back(line.substr(p + 1));
			break;
		}
		size_t end = line.find(' ', p);
		out.params.emplace_back(line.substr(p, end == std::string_view::npos ? std::string_view::npos : end - p));
		if (end == std::string_view::npos)
			break;
		p = end;
	}
	return out;
}

std::vector<std::string> MapTwitchBadges(std::string_view badges)
{
	std::vector<std::string> out;
	auto add = [&](const char *name) {
		for (const auto &b : out) {
			if (b == name)
				return;
		}
		out.emplace_back(name);
	};
	while (!badges.empty()) {
		size_t comma = badges.find(',');
		std::string_view item = badges.substr(0, comma);
		std::string_view name = item.substr(0, item.find('/'));
		if (name == "broadcaster")
			add("broadcaster");
		else if (name == "moderator")
			add("moderator");
		else if (name == "subscriber" || name == "founder")
			add("subscriber");
		else if (name == "vip")
			add("vip");
		else if (name == "partner")
			add("verified");
		if (comma == std::string_view::npos)
			break;
		badges.remove_prefix(comma + 1);
	}
	return out;
}

std::string NormalizeTwitchChannel(std::string_view channel)
{
	channel = TrimAscii(channel);
	if (!channel.empty() && channel.front() == '#')
		channel.remove_prefix(1);
	return ToLowerAscii(channel);
}

bool IsValidTwitchChannel(std::string_view normalized)
{
	if (normalized.empty() || normalized.size() > 25)
		return false;
	for (char c : normalized) {
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
			return false;
	}
	return true;
}

bool IsPermanentTwitchNotice(const TwitchEvent &ev)
{
	if (ev.type != TwitchEvent::Type::Notice)
		return false;
	static const char *const kIds[] = {"msg_channel_suspended", "msg_banned", "msg_room_not_found",
					   "tos_ban", "msg_channel_blocked", "invalid_channel"};
	for (const char *id : kIds) {
		if (ev.notice_id == id)
			return true;
	}
	if (ev.notice_id.empty()) {
		// Login problems arrive as untagged NOTICEs.
		const std::string &t = ev.param;
		if (t.find("Login authentication failed") != std::string::npos ||
		    t.find("Improperly formatted auth") != std::string::npos ||
		    t.find("Login unsuccessful") != std::string::npos)
			return true;
	}
	return false;
}

namespace {

std::string ChannelParam(const IrcLine &l)
{
	if (l.params.empty())
		return {};
	std::string_view c = l.params[0];
	if (c.empty() || c.front() != '#')
		return {};
	c.remove_prefix(1);
	return ToLowerAscii(c);
}

bool ParseInt64(std::string_view s, int64_t &out)
{
	if (s.empty())
		return false;
	auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
	return ec == std::errc() && ptr == s.data() + s.size();
}

} // namespace

TwitchEvent ParseTwitchLine(std::string_view line, int64_t fallback_now_ms)
{
	TwitchEvent ev;
	auto parsed = ParseIrcLine(line);
	if (!parsed)
		return ev;
	const IrcLine &l = *parsed;
	const std::string &cmd = l.command;
	ev.channel = ChannelParam(l);

	auto tag = [&](std::string_view k) -> std::string {
		const std::string *v = l.Tag(k);
		return v ? *v : std::string();
	};

	if (cmd == "PRIVMSG") {
		if (l.params.size() < 2 || ev.channel.empty())
			return ev;
		ev.type = TwitchEvent::Type::Message;
		ChatMessage &m = ev.message;
		m.platform = Platform::Twitch;
		m.id = tag("id");
		m.author_id = tag("user-id");
		m.author = tag("display-name");
		if (TrimAscii(m.author).empty())
			m.author = l.nick;
		m.color = tag("color");
		m.badges = MapTwitchBadges(tag("badges"));
		std::string_view text = l.params.back();
		constexpr std::string_view kAction = "\x01" "ACTION ";
		if (text.size() >= kAction.size() && text.substr(0, kAction.size()) == kAction) {
			text.remove_prefix(kAction.size());
			if (!text.empty() && text.back() == '\x01')
				text.remove_suffix(1);
			ev.action = true;
		}
		m.text = std::string(text);
		int64_t ts = 0;
		m.timestamp_ms = ParseInt64(tag("tmi-sent-ts"), ts) && ts > 0 ? ts : fallback_now_ms;
		return ev;
	}
	if (cmd == "CLEARMSG") {
		ev.target_id = tag("target-msg-id");
		if (!ev.target_id.empty())
			ev.type = TwitchEvent::Type::DeleteMessage;
		return ev;
	}
	if (cmd == "CLEARCHAT") {
		ev.target_id = tag("target-user-id");
		if (l.params.size() >= 2)
			ev.target_login = ToLowerAscii(l.params[1]);
		if (!ev.target_id.empty() || !ev.target_login.empty())
			ev.type = TwitchEvent::Type::ClearUser;
		else
			ev.type = TwitchEvent::Type::ClearAll;
		return ev;
	}
	if (cmd == "PING" || cmd == "PONG") {
		ev.type = cmd == "PING" ? TwitchEvent::Type::Ping : TwitchEvent::Type::Pong;
		if (!l.params.empty())
			ev.param = l.params.back();
		return ev;
	}
	if (cmd == "RECONNECT") {
		ev.type = TwitchEvent::Type::Reconnect;
		return ev;
	}
	if (cmd == "NOTICE") {
		ev.type = TwitchEvent::Type::Notice;
		ev.notice_id = tag("msg-id");
		if (!l.params.empty())
			ev.param = l.params.back();
		return ev;
	}
	if (cmd == "001") {
		ev.type = TwitchEvent::Type::Welcome;
		return ev;
	}
	if (cmd == "JOIN") {
		if (ev.channel.empty())
			return ev;
		ev.type = TwitchEvent::Type::Join;
		ev.nick = ToLowerAscii(l.nick);
		return ev;
	}
	if (cmd == "ROOMSTATE") {
		if (!ev.channel.empty())
			ev.type = TwitchEvent::Type::RoomState;
		return ev;
	}
	return ev;
}

} // namespace tandem::chat
