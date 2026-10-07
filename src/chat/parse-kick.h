// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - pure parser for Kick's Pusher WebSocket events. Never throws.
#pragma once

#include "chat-types.h"

#include <optional>
#include <string>
#include <string_view>

namespace tandem::chat {

struct KickEvent {
	enum class Type {
		None,
		Malformed, // not JSON, or a known event whose payload did not match the expected shape
		Unknown,   // well-formed event we do not handle
		ConnectionEstablished,
		SubscriptionSucceeded,
		Ping,
		Pong,
		PusherError,
		Message,
		DeleteMessage,
		ClearUser,
		ClearAll,
	};
	Type type = Type::None;
	std::string event_name;   // raw Pusher event name
	std::string channel;      // Pusher channel, when present
	ChatMessage message;      // Message
	std::string target_id;    // DeleteMessage: message id; ClearUser: user id
	int activity_timeout_s = 120; // ConnectionEstablished
	int error_code = 0;       // PusherError (0 when absent)
	std::string error_text;   // PusherError / Malformed: short description
};

KickEvent ParseKickPusher(std::string_view text, int64_t fallback_now_ms);

// "[emote:12345:Name]" -> "Name"; anything malformed is left untouched.
std::string StripKickEmotes(std::string_view text);

// Slug as in kick.com/<slug>: [A-Za-z0-9_-], 1-64 chars.
bool IsValidKickSlug(std::string_view slug);
// Chatroom ids are positive decimal integers.
bool IsValidKickChatroomId(std::string_view id);

// Reads chatroom.id from a https://kick.com/api/v2/channels/<slug> response.
std::optional<std::string> ParseKickChannelChatroomId(std::string_view body);

std::string KickSubscribeMessage(const std::string &chatroom_id);
std::string KickPingMessage();
std::string KickPongMessage();

} // namespace tandem::chat
