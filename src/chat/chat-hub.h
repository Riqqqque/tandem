// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - fan-out of chat events to independent bounded subscriber queues.
#pragma once

#include "chat-types.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>

namespace tandem::chat {

// Thread-safe. Providers Post() from their worker threads; each consumer (dock, overlay)
// owns a subscription with its own bounded queue so a slow consumer never blocks a
// provider or another consumer. When a queue is full the oldest chat messages are dropped;
// status/quota/delete events are kept so state never goes stale.
class ChatHub final : public ChatSink {
public:
	explicit ChatHub(size_t queue_limit = 2000, size_t history_limit = 200);

	void Post(ChatEvent &&ev) override;

	int Subscribe();
	void Unsubscribe(int id);

	// Moves up to max_events queued events into out. Returns the number moved.
	size_t Drain(int id, std::vector<ChatEvent> &out, size_t max_events = SIZE_MAX);

	// Blocks until the subscription has events, it is woken, or the timeout expires.
	bool Wait(int id, std::chrono::milliseconds timeout);
	void WakeAll();

	// Most recent chat messages (oldest first), for consumers that attach late.
	std::vector<ChatMessage> History() const;
	void ClearHistory();

	// Latest status per platform.
	ProviderStatus Status(Platform p) const;

	size_t dropped() const;

private:
	struct Sub {
		std::deque<ChatEvent> q;
		bool woken = false;
	};
	mutable std::mutex mu_;
	std::condition_variable cv_;
	std::map<int, Sub> subs_;
	int next_id_ = 1;
	size_t queue_limit_;
	size_t history_limit_;
	std::deque<ChatMessage> history_;
	ProviderStatus status_[kPlatformCount];
	size_t dropped_ = 0;
};

} // namespace tandem::chat
