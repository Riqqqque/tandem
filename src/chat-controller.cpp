// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat-controller.h"

#include "pch.h"
#include "overlay/overlay-server.h"

#include <ctime>
#include <thread>
#include <vector>

using namespace tandem::chat;

static ChatPlatformConfig &PlatformConfig(ChatConfig &c, int index)
{
	switch (static_cast<Platform>(index)) {
	case Platform::Twitch:
		return c.twitch;
	case Platform::Kick:
		return c.kick;
	case Platform::YouTube:
	default:
		return c.youtube;
	}
}

static bool SameSettings(const ChatPlatformConfig &a, const ChatPlatformConfig &b)
{
	return a.enabled == b.enabled && a.channel == b.channel && a.extra == b.extra;
}

std::string YouTubeQuotaDay()
{
	// Google resets the Data API quota at midnight Pacific time. US DST runs from the
	// second Sunday of March to the first Sunday of November.
	time_t now = time(nullptr);
	time_t pst = now - 8 * 3600;
	struct tm t {};
#ifdef _WIN32
	gmtime_s(&t, &pst);
#else
	gmtime_r(&pst, &t);
#endif
	auto nthSunday = [&](int month, int n) {
		struct tm first {};
		first.tm_year = t.tm_year;
		first.tm_mon = month;
		first.tm_mday = 1;
		first.tm_hour = 12;
		time_t ft = mktime(&first);
		struct tm f {};
#ifdef _WIN32
		localtime_s(&f, &ft);
#else
		localtime_r(&ft, &f);
#endif
		int firstSunday = 1 + (7 - f.tm_wday) % 7;
		return firstSunday + 7 * (n - 1);
	};
	bool dst = false;
	int mon = t.tm_mon, day = t.tm_mday;
	if (mon > 2 && mon < 10)
		dst = true;
	else if (mon == 2)
		dst = day >= nthSunday(2, 2);
	else if (mon == 10)
		dst = day < nthSunday(10, 1);
	if (dst) {
		time_t pdt = now - 7 * 3600;
#ifdef _WIN32
		gmtime_s(&t, &pdt);
#else
		gmtime_r(&pdt, &t);
#endif
	}
	char buf[16];
	snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
	return buf;
}

ChatController::ChatController() : overlay_(std::make_unique<tandem::overlay::OverlayServer>()) {}

ChatController::~ChatController()
{
	Shutdown();
}

ChatController &GetChatController()
{
	static ChatController instance;
	return instance;
}

void ChatController::StartProvider(int index)
{
	StopProvider(index);
	auto &chat = GlobalMultiOutputConfig().chat;
	auto &cfg = PlatformConfig(chat, index);
	applied_[index] = cfg;
	if (index == static_cast<int>(Platform::YouTube))
		appliedYouTubeKey_ = chat.youtubeApiKey;
	if (!cfg.enabled || (cfg.channel.empty() && cfg.extra.empty()))
		return;

	switch (static_cast<Platform>(index)) {
	case Platform::Twitch:
		providers_[index] = CreateTwitchProvider({cfg.channel}, &hub_);
		break;
	case Platform::Kick:
		providers_[index] = CreateKickProvider({cfg.channel, cfg.extra}, &hub_);
		break;
	case Platform::YouTube: {
		auto today = YouTubeQuotaDay();
		if (chat.youtubeQuotaDay != today) {
			chat.youtubeQuotaDay = today;
			chat.youtubeQuotaUsed = 0;
		}
		YouTubeChatConfig yt;
		yt.api_key = chat.youtubeApiKey;
		yt.video = cfg.channel;
		yt.channel_id = cfg.extra;
		yt.quota_used_today = chat.youtubeQuotaUsed;
		yt.on_quota = [](int64_t used) {
			GetGlobalService().RunInUIThread([used]() { GetChatController().OnQuota(used); });
		};
		providers_[index] = CreateYouTubeProvider(std::move(yt), &hub_);
		break;
	}
	}
	if (providers_[index])
		providers_[index]->Start();
}

void ChatController::StopProvider(int index)
{
	if (providers_[index]) {
		providers_[index]->Stop();
		providers_[index].reset();
	}
}

void ChatController::OnQuota(int64_t used)
{
	auto &chat = GlobalMultiOutputConfig().chat;
	auto today = YouTubeQuotaDay();
	if (chat.youtubeQuotaDay != today)
		chat.youtubeQuotaDay = today;
	chat.youtubeQuotaUsed = used;
	// Persist occasionally so the estimate survives an OBS restart.
	static int64_t lastSaved = 0;
	if (used - lastSaved >= 50 || used < lastSaved) {
		lastSaved = used;
		SaveMultiOutputConfig();
	}
}

void ChatController::StartChat()
{
	if (shutdown_)
		return;
	running_ = true;
	for (int i = 0; i < kPlatformCount; ++i)
		StartProvider(i);
}

void ChatController::StopChat()
{
	running_ = false;
	startedWithStream_ = false;
	// Each Stop() can take a moment to close its connection; stop them side by side.
	std::vector<std::thread> stoppers;
	for (auto &provider : providers_) {
		if (provider)
			stoppers.emplace_back([p = std::move(provider)]() { p->Stop(); });
	}
	for (auto &t : stoppers)
		t.join();
}

void ChatController::ApplyConfig()
{
	if (shutdown_)
		return;
	auto &chat = GlobalMultiOutputConfig().chat;
	if (running_) {
		for (int i = 0; i < kPlatformCount; ++i) {
			auto &cfg = PlatformConfig(chat, i);
			bool keyChanged = i == static_cast<int>(Platform::YouTube) && appliedYouTubeKey_ != chat.youtubeApiKey;
			if (!SameSettings(cfg, applied_[i]) || keyChanged)
				StartProvider(i);
		}
	}
	UpdateOverlay();
}

void ChatController::UpdateOverlay()
{
	auto &chat = GlobalMultiOutputConfig().chat;
	bool want = chat.overlayEnabled && !shutdown_;
	if (overlay_->running() && (!want || overlayPort_ != chat.overlayPort))
		overlay_->Stop();
	if (want && !overlay_->running()) {
		tandem::overlay::OverlayOptions opt;
		opt.port = chat.overlayPort;
		overlayError_.clear();
		if (overlay_->Start(&hub_, opt, &overlayError_)) {
			overlayPort_ = chat.overlayPort;
			blog(LOG_INFO, TAG "Chat overlay listening on 127.0.0.1:%d", overlayPort_);
		} else {
			blog(LOG_WARNING, TAG "Chat overlay could not start: %s", overlayError_.c_str());
		}
	}
	if (!want)
		overlayError_.clear();
}

bool ChatController::OverlayRunning() const
{
	return overlay_ && overlay_->running();
}

std::string ChatController::OverlayUrl() const
{
	int port = OverlayRunning() ? overlay_->port() : GlobalMultiOutputConfig().chat.overlayPort;
	return "http://127.0.0.1:" + std::to_string(port) + "/";
}

void ChatController::OnFrontendEvent(obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		UpdateOverlay();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		if (!running_ && GlobalMultiOutputConfig().chat.startWithStream) {
			StartChat();
			startedWithStream_ = true;
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (running_ && startedWithStream_)
			StopChat();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		StopChat();
		break;
	default:
		break;
	}
}

void ChatController::Shutdown()
{
	if (shutdown_)
		return;
	shutdown_ = true;
	StopChat();
	if (overlay_)
		overlay_->Stop();
}
