// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - worker thread, stop flag and status plumbing shared by the chat providers.
#pragma once

#include "chat-types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace tandem::chat {

class ProviderBase : public ChatProvider {
public:
	ProviderBase(Platform platform, ChatSink *sink) : platform_(platform), sink_(sink) {}
	~ProviderBase() override;

	void Start() override;
	void Stop() override;
	Platform GetPlatform() const override { return platform_; }

protected:
	// Runs on the worker thread until it returns or stopping() becomes true.
	virtual void Run() = 0;

	bool stopping() const { return stop_.load(); }
	const std::atomic<bool> *stop_flag() const { return &stop_; }

	// Sleeps up to `d`, returning false early when Stop() is requested.
	bool WaitFor(std::chrono::milliseconds d);

	// Posts a status event; consecutive identical statuses are suppressed.
	void PostStatus(ProviderState state, const std::string &detail = {});
	void PostChatMessage(ChatMessage &&msg);
	void PostDelete(const std::string &message_id);
	void PostClearUser(const std::string &author_id);
	void PostClearAll();
	void PostQuota(int64_t used);
	// Reports the platform's numeric channel/user id (used to load 7TV/BTTV/FFZ emotes).
	void PostChannelInfo(const std::string &channel_id);

	const char *name() const { return PlatformName(platform_); }

private:
	void ThreadMain();

	Platform platform_;
	ChatSink *sink_;
	std::thread thread_;
	std::atomic<bool> stop_{false};
	std::mutex mu_;
	std::condition_variable cv_;
	std::mutex start_mu_;
	ProviderState last_state_ = ProviderState::Stopped;
	std::string last_detail_;
	bool any_status_ = false;
};

// "retry in 5s" style suffix.
std::string RetryText(std::chrono::milliseconds d);

} // namespace tandem::chat
