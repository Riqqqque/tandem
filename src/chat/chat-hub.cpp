// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat-hub.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace tandem::chat {

const char *PlatformName(Platform p)
{
	switch (p) {
	case Platform::Twitch:
		return "Twitch";
	case Platform::Kick:
		return "Kick";
	case Platform::YouTube:
		return "YouTube";
	}
	return "?";
}

const char *PlatformKey(Platform p)
{
	switch (p) {
	case Platform::Twitch:
		return "twitch";
	case Platform::Kick:
		return "kick";
	case Platform::YouTube:
		return "youtube";
	}
	return "unknown";
}

const char *ProviderStateName(ProviderState s)
{
	switch (s) {
	case ProviderState::Stopped:
		return "stopped";
	case ProviderState::Connecting:
		return "connecting";
	case ProviderState::Connected:
		return "connected";
	case ProviderState::Reconnecting:
		return "reconnecting";
	case ProviderState::Error:
		return "error";
	}
	return "unknown";
}

static LogFn g_log = nullptr;

void SetLogFunction(LogFn fn)
{
	g_log = fn;
}

void Log(LogLevel level, const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (g_log)
		g_log(level, buf);
	else
		fprintf(stderr, "%s\n", buf);
}

ChatHub::ChatHub(size_t queue_limit, size_t history_limit) : queue_limit_(queue_limit), history_limit_(history_limit) {}

void ChatHub::Post(ChatEvent &&ev)
{
	{
		std::lock_guard lock(mu_);
		switch (ev.kind) {
		case ChatEvent::Kind::Message:
			history_.push_back(ev.message);
			while (history_.size() > history_limit_)
				history_.pop_front();
			break;
		case ChatEvent::Kind::DeleteMessage:
			history_.erase(std::remove_if(history_.begin(), history_.end(),
						      [&](const ChatMessage &m) {
							      return m.platform == ev.platform && m.id == ev.target_id;
						      }),
				       history_.end());
			break;
		case ChatEvent::Kind::ClearUser:
			history_.erase(std::remove_if(history_.begin(), history_.end(),
						      [&](const ChatMessage &m) {
							      return m.platform == ev.platform &&
								     m.author_id == ev.target_id;
						      }),
				       history_.end());
			break;
		case ChatEvent::Kind::ClearAll:
			history_.erase(std::remove_if(history_.begin(), history_.end(),
						      [&](const ChatMessage &m) { return m.platform == ev.platform; }),
				       history_.end());
			break;
		case ChatEvent::Kind::Status:
			status_[static_cast<int>(ev.platform)] = ev.status;
			break;
		case ChatEvent::Kind::Quota:
		case ChatEvent::Kind::ChannelInfo:
			break;
		}

		for (auto &[id, sub] : subs_) {
			if (sub.q.size() >= queue_limit_) {
				// Drop the oldest chat message; keep control events.
				auto it = std::find_if(sub.q.begin(), sub.q.end(), [](const ChatEvent &e) {
					return e.kind == ChatEvent::Kind::Message;
				});
				if (it != sub.q.end()) {
					sub.q.erase(it);
					++dropped_;
				} else if (ev.kind == ChatEvent::Kind::Message) {
					++dropped_;
					continue;
				}
			}
			sub.q.push_back(ev);
		}
	}
	cv_.notify_all();
}

int ChatHub::Subscribe()
{
	std::lock_guard lock(mu_);
	int id = next_id_++;
	subs_[id];
	return id;
}

void ChatHub::Unsubscribe(int id)
{
	{
		std::lock_guard lock(mu_);
		subs_.erase(id);
	}
	cv_.notify_all();
}

size_t ChatHub::Drain(int id, std::vector<ChatEvent> &out, size_t max_events)
{
	std::lock_guard lock(mu_);
	auto it = subs_.find(id);
	if (it == subs_.end())
		return 0;
	auto &q = it->second.q;
	size_t n = std::min(max_events, q.size());
	for (size_t i = 0; i < n; ++i) {
		out.push_back(std::move(q.front()));
		q.pop_front();
	}
	return n;
}

bool ChatHub::Wait(int id, std::chrono::milliseconds timeout)
{
	std::unique_lock lock(mu_);
	auto ready = [&] {
		auto it = subs_.find(id);
		return it == subs_.end() || !it->second.q.empty() || it->second.woken;
	};
	cv_.wait_for(lock, timeout, ready);
	auto it = subs_.find(id);
	if (it == subs_.end())
		return false;
	bool woken = it->second.woken;
	it->second.woken = false;
	return !it->second.q.empty() || woken;
}

void ChatHub::WakeAll()
{
	{
		std::lock_guard lock(mu_);
		for (auto &[id, sub] : subs_)
			sub.woken = true;
	}
	cv_.notify_all();
}

std::vector<ChatMessage> ChatHub::History() const
{
	std::lock_guard lock(mu_);
	return {history_.begin(), history_.end()};
}

void ChatHub::ClearHistory()
{
	std::lock_guard lock(mu_);
	history_.clear();
}

ProviderStatus ChatHub::Status(Platform p) const
{
	std::lock_guard lock(mu_);
	return status_[static_cast<int>(p)];
}

size_t ChatHub::dropped() const
{
	std::lock_guard lock(mu_);
	return dropped_;
}

} // namespace tandem::chat
