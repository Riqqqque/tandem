// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - pure parser for Twitch IRC (IRCv3 tags) lines. Never throws.
#pragma once

#include "chat-types.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tandem::chat {

struct IrcLine {
	std::vector<std::pair<std::string, std::string>> tags; // values already unescaped
	std::string prefix; // without the leading ':'
	std::string nick;   // part of the prefix before '!', or empty
	std::string command;
	std::vector<std::string> params; // the trailing parameter, when present, is the last element

	const std::string *Tag(std::string_view key) const;
};

// Splits one IRC line (CR/LF optional). Returns nullopt for empty or malformed lines.
std::optional<IrcLine> ParseIrcLine(std::string_view line);

// IRCv3 tag value unescaping: \s -> space, \: -> ';', \\ -> '\', \r -> CR, \n -> LF.
std::string UnescapeIrcTagValue(std::string_view v);

// "broadcaster/1,moderator/1,subscriber/12" -> {"broadcaster","moderator","subscriber"}
std::vector<std::string> MapTwitchBadges(std::string_view badges);

struct TwitchEvent {
	enum class Type {
		None,          // ignored or malformed line
		Message,       // PRIVMSG (including /me actions)
		DeleteMessage, // CLEARMSG
		ClearUser,     // CLEARCHAT with a target user
		ClearAll,      // CLEARCHAT without a target
		Ping,
		Pong,
		Reconnect,
		Notice,
		Welcome, // 001
		Join,
		RoomState,
	};
	Type type = Type::None;
	ChatMessage message;    // Message
	bool action = false;    // Message: sent with /me
	std::string target_id;  // DeleteMessage: message id; ClearUser: user id
	std::string target_login; // ClearUser: login name from the trailing parameter
	std::string param;      // Ping/Pong: the parameter to echo; Notice: the text
	std::string notice_id;  // Notice: msg-id tag
	std::string channel;    // channel without '#', lowercase, when the command has one
	std::string nick;       // Join: who joined
	std::string room_id;    // Message/RoomState: the channel's numeric Twitch id
};

// Twitch's "emotes" tag (id:start-end,.../...) -> spans over the UTF-8 text. Positions in the tag
// count Unicode code points. Malformed or overlapping entries are dropped.
std::vector<EmoteSpan> ParseTwitchEmotes(std::string_view tag, std::string_view text);

// fallback_now_ms is used when the line has no usable tmi-sent-ts.
TwitchEvent ParseTwitchLine(std::string_view line, int64_t fallback_now_ms);

// Lowercases, trims, and removes a leading '#'.
std::string NormalizeTwitchChannel(std::string_view channel);
// 1-25 characters (older accounts can be shorter than 4) of [a-z0-9_] (after normalization).
bool IsValidTwitchChannel(std::string_view normalized);

// True for NOTICEs that mean retrying cannot help (suspended channel, failed login, ...).
bool IsPermanentTwitchNotice(const TwitchEvent &ev);

} // namespace tandem::chat
