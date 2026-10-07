// SPDX-License-Identifier: GPL-2.0-or-later
#include "provider-base.h"

#include <exception>

namespace tandem::chat {

ProviderBase::~ProviderBase()
{
	// Derived classes must call Stop() in their own destructor, before their members are
	// destroyed; this is only a safety net.
	if (thread_.joinable()) {
		stop_ = true;
		cv_.notify_all();
		thread_.join();
	}
}

void ProviderBase::Start()
{
	std::lock_guard lock(start_mu_);
	if (thread_.joinable())
		return;
	stop_ = false;
	any_status_ = false;
	thread_ = std::thread([this] { ThreadMain(); });
}

void ProviderBase::Stop()
{
	std::lock_guard lock(start_mu_);
	if (!thread_.joinable())
		return;
	{
		std::lock_guard l(mu_);
		stop_ = true;
	}
	cv_.notify_all();
	thread_.join();
	thread_ = std::thread();
	PostStatus(ProviderState::Stopped);
}

void ProviderBase::ThreadMain()
{
	try {
		Run();
	} catch (const std::exception &e) {
		Log(LogLevel::Error, "[chat/%s] worker failed: %s", PlatformKey(platform_), e.what());
		PostStatus(ProviderState::Error, std::string("Internal error: ") + e.what());
	} catch (...) {
		Log(LogLevel::Error, "[chat/%s] worker failed with an unknown exception", PlatformKey(platform_));
		PostStatus(ProviderState::Error, "Internal error");
	}
}

bool ProviderBase::WaitFor(std::chrono::milliseconds d)
{
	std::unique_lock lock(mu_);
	return !cv_.wait_for(lock, d, [this] { return stop_.load(); });
}

void ProviderBase::PostStatus(ProviderState state, const std::string &detail)
{
	{
		std::lock_guard lock(mu_);
		if (any_status_ && state == last_state_ && detail == last_detail_)
			return;
		any_status_ = true;
		last_state_ = state;
		last_detail_ = detail;
	}
	Log(state == ProviderState::Error ? LogLevel::Warning : LogLevel::Info, "[chat/%s] %s%s%s",
	    PlatformKey(platform_), ProviderStateName(state), detail.empty() ? "" : ": ", detail.c_str());
	ChatEvent ev;
	ev.kind = ChatEvent::Kind::Status;
	ev.platform = platform_;
	ev.status.state = state;
	ev.status.detail = detail;
	sink_->Post(std::move(ev));
}

void ProviderBase::PostChatMessage(ChatMessage &&msg)
{
	ChatEvent ev;
	ev.kind = ChatEvent::Kind::Message;
	ev.platform = platform_;
	msg.platform = platform_;
	ev.message = std::move(msg);
	sink_->Post(std::move(ev));
}

void ProviderBase::PostDelete(const std::string &message_id)
{
	ChatEvent ev;
	ev.kind = ChatEvent::Kind::DeleteMessage;
	ev.platform = platform_;
	ev.target_id = message_id;
	sink_->Post(std::move(ev));
}

void ProviderBase::PostClearUser(const std::string &author_id)
{
	ChatEvent ev;
	ev.kind = ChatEvent::Kind::ClearUser;
	ev.platform = platform_;
	ev.target_id = author_id;
	sink_->Post(std::move(ev));
}

void ProviderBase::PostClearAll()
{
	ChatEvent ev;
	ev.kind = ChatEvent::Kind::ClearAll;
	ev.platform = platform_;
	sink_->Post(std::move(ev));
}

void ProviderBase::PostQuota(int64_t used)
{
	ChatEvent ev;
	ev.kind = ChatEvent::Kind::Quota;
	ev.platform = platform_;
	ev.quota_used = used;
	sink_->Post(std::move(ev));
}

std::string RetryText(std::chrono::milliseconds d)
{
	long long s = (d.count() + 999) / 1000;
	return "retry in " + std::to_string(s) + "s";
}

} // namespace tandem::chat
