// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - common chat model shared by providers, the dock and the overlay.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tandem::chat {

enum class Platform { Twitch = 0, Kick = 1, YouTube = 2 };
constexpr int kPlatformCount = 3;

const char *PlatformName(Platform p);    // "Twitch", "Kick", "YouTube"
const char *PlatformKey(Platform p);     // "twitch", "kick", "youtube"

// An emote inside ChatMessage::text: [begin, end) are UTF-8 byte offsets.
struct EmoteSpan {
	size_t begin = 0;
	size_t end = 0;
	std::string name;
	std::string url;  // image URL (PNG, GIF or WebP)
	std::string source; // "twitch", "kick", "7tv", "bttv", "ffz"
};

struct ChatMessage {
	Platform platform = Platform::Twitch;
	std::string id;           // platform message id (dedupe / deletes)
	std::string author_id;    // platform user id when known (for "clear user")
	std::string author;       // display name
	std::string color;        // "#RRGGBB" or empty
	std::vector<std::string> badges; // "broadcaster", "moderator", "subscriber", "vip", "verified", "owner", "member"
	std::string text;         // plain text; emotes are left as their text form
	std::vector<EmoteSpan> emotes; // sorted, non-overlapping; empty when there are none
	int64_t timestamp_ms = 0; // unix epoch milliseconds
};

enum class ProviderState { Stopped, Connecting, Connected, Reconnecting, Error };

const char *ProviderStateName(ProviderState s);

struct ProviderStatus {
	ProviderState state = ProviderState::Stopped;
	std::string detail; // human readable reason; never contains secrets
};

// Events flowing from providers to consumers. Exactly one payload is meaningful per kind.
struct ChatEvent {
	// ChannelInfo: target_id carries the platform's numeric channel/user id once known, so
	// third-party emote sets (7TV, BetterTTV, FrankerFaceZ) can be loaded for that channel.
	enum class Kind { Message, DeleteMessage, ClearUser, ClearAll, Status, Quota, ChannelInfo };
	Kind kind = Kind::Message;
	Platform platform = Platform::Twitch;
	ChatMessage message;      // Message
	std::string target_id;    // DeleteMessage: message id; ClearUser: author_id
	ProviderStatus status;    // Status
	int64_t quota_used = 0;   // Quota: estimated units used today (YouTube)
};

// Sink implemented by the hub. Providers call it from their own worker threads.
class ChatSink {
public:
	virtual ~ChatSink() = default;
	virtual void Post(ChatEvent &&ev) = 0;
};

class ChatProvider {
public:
	virtual ~ChatProvider() = default;
	// Start is non-blocking: it spawns the provider's worker thread.
	virtual void Start() = 0;
	// Stop blocks until the worker thread exits (bounded, a few seconds at most).
	virtual void Stop() = 0;
	virtual Platform GetPlatform() const = 0;
};

struct TwitchChatConfig {
	std::string channel; // login name, with or without leading '#'
};

struct KickChatConfig {
	std::string channel;     // slug as in kick.com/<slug>
	std::string chatroom_id; // optional manual override when slug lookup is blocked
};

struct YouTubeChatConfig {
	std::string api_key;       // user's own Data API v3 key; sent only in X-Goog-Api-Key
	std::string video;         // video id or URL (preferred)
	std::string channel_id;    // used only when video is empty (costs a search call)
	int64_t quota_used_today = 0;           // carried over across restarts on the same day
	std::function<void(int64_t)> on_quota;  // called with the new running total (any thread)
};

// Logging hook. The plugin routes this to blog(); tests route it to stderr.
enum class LogLevel { Debug, Info, Warning, Error };
using LogFn = void (*)(LogLevel, const char *msg);
void SetLogFunction(LogFn fn);
void Log(LogLevel level, const char *fmt, ...);

std::unique_ptr<ChatProvider> CreateTwitchProvider(TwitchChatConfig cfg, ChatSink *sink);
std::unique_ptr<ChatProvider> CreateKickProvider(KickChatConfig cfg, ChatSink *sink);
std::unique_ptr<ChatProvider> CreateYouTubeProvider(YouTubeChatConfig cfg, ChatSink *sink);

} // namespace tandem::chat
