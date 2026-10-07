// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - third-party emotes (7TV, BetterTTV, FrankerFaceZ).
//
// Viewers with those browser extensions type emote names as plain words. EmoteAnnotator sits
// between the providers and the hub, loads the global sets and each channel's sets in the
// background, and marks matching words in every message as EmoteSpans.
#pragma once

#include "chat-types.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace tandem::chat {

struct EmoteRef {
	std::string url;
	std::string source; // "7tv", "bttv", "ffz"
};
using EmoteMap = std::unordered_map<std::string, EmoteRef>;

// Pure parsers for the providers' public APIs; malformed input yields an empty map.
EmoteMap Parse7tvEmoteSet(std::string_view body);    // /v3/emote-sets/<id> or the "emote_set" of /v3/users/...
EmoteMap Parse7tvUser(std::string_view body);        // /v3/users/{twitch|kick|youtube}/<id>
EmoteMap ParseBttvEmotes(std::string_view body);     // /3/cached/emotes/global (array)
EmoteMap ParseBttvUser(std::string_view body);       // /3/cached/users/twitch/<id>
EmoteMap ParseFfzRoom(std::string_view body);        // /v1/room/id/<id>
EmoteMap ParseFfzGlobal(std::string_view body);      // /v1/set/global (default_sets only)

// True for https URLs on the emote CDNs Tandem uses. Anything else is never fetched or shown.
bool IsTrustedEmoteUrl(std::string_view url);
// The same hosts as a space-separated CSP source list.
const char *EmoteCspSources();

// Adds spans for whole words found in `map`, skipping ranges already covered (native emotes).
void AnnotateWords(ChatMessage &m, const EmoteMap &map);

class EmoteAnnotator final : public ChatSink {
public:
	explicit EmoteAnnotator(ChatSink *next);
	~EmoteAnnotator() override;

	void SetEnabled(bool enabled);
	// Stops the loader thread (bounded by the HTTP timeout). Safe to call more than once.
	void Shutdown();
	void Post(ChatEvent &&ev) override;

private:
	struct Job {
		Platform platform;
		std::string channel_id; // empty = global sets
	};
	void WorkerMain();
	void Load(const Job &job);
	void Rebuild(Platform p); // caller holds mu_

	ChatSink *next_;
	std::atomic<bool> enabled_{true};
	std::atomic<bool> stop_{false};
	std::thread worker_;
	std::mutex mu_;
	std::condition_variable cv_;
	std::deque<Job> jobs_;
	bool globals_requested_ = false;
	std::string channel_ids_[kPlatformCount];

	// Sources in priority order: channel 7TV, BTTV, FFZ, then global 7TV, BTTV, FFZ.
	EmoteMap channel7tv_[kPlatformCount], channelBttv_[kPlatformCount], channelFfz_[kPlatformCount];
	EmoteMap global7tv_, globalBttv_, globalFfz_;
	EmoteMap effective_[kPlatformCount];
};

} // namespace tandem::chat
