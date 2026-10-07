// SPDX-License-Identifier: GPL-2.0-or-later
// Provider lifecycle checks that need no network: configuration errors are reported
// before any connection is attempted.
#include "test-framework.h"

#include "chat-hub.h"

#include <chrono>

using namespace tandem::chat;

namespace {

std::vector<ProviderStatus> WaitStatuses(ChatHub &hub, int sub, size_t want, std::chrono::milliseconds timeout)
{
	std::vector<ProviderStatus> out;
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (out.size() < want && std::chrono::steady_clock::now() < deadline) {
		hub.Wait(sub, std::chrono::milliseconds(50));
		std::vector<ChatEvent> evs;
		hub.Drain(sub, evs);
		for (const auto &e : evs) {
			if (e.kind == ChatEvent::Kind::Status)
				out.push_back(e.status);
		}
	}
	return out;
}

void CheckRestartCycle(std::unique_ptr<ChatProvider> p, ChatHub &hub, int sub, const std::string &expected_error)
{
	for (int round = 0; round < 2; ++round) {
		p->Start();
		p->Start(); // second Start while running is ignored
		auto s = WaitStatuses(hub, sub, 1, std::chrono::seconds(5));
		CHECK_EQ(s.size(), size_t(1));
		if (!s.empty()) {
			CHECK(s[0].state == ProviderState::Error);
			CHECK_EQ(s[0].detail, expected_error);
		}
		auto t0 = std::chrono::steady_clock::now();
		p->Stop();
		p->Stop(); // idempotent
		CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
		s = WaitStatuses(hub, sub, 1, std::chrono::seconds(1));
		CHECK_EQ(s.size(), size_t(1));
		if (!s.empty())
			CHECK(s[0].state == ProviderState::Stopped);
	}
}

} // namespace

TEST_CASE(provider_twitch_invalid_channel_restart)
{
	ChatHub hub;
	int sub = hub.Subscribe();
	CheckRestartCycle(CreateTwitchProvider({"no spaces allowed"}, &hub), hub, sub,
			  "Invalid Twitch channel name (1-25 letters, digits or _)");
	CHECK(hub.Status(Platform::Twitch).state == ProviderState::Stopped);
}

TEST_CASE(provider_kick_invalid_config)
{
	ChatHub hub;
	int sub = hub.Subscribe();
	KickChatConfig cfg;
	cfg.channel = "fine";
	cfg.chatroom_id = "12ab";
	CheckRestartCycle(CreateKickProvider(cfg, &hub), hub, sub, "Invalid Kick chatroom ID (digits only)");
}

TEST_CASE(provider_youtube_missing_key_and_bad_video)
{
	ChatHub hub;
	int sub = hub.Subscribe();
	YouTubeChatConfig cfg;
	cfg.video = "abcDEF12345";
	CheckRestartCycle(CreateYouTubeProvider(cfg, &hub), hub, sub, "YouTube API key is missing");

	YouTubeChatConfig bad;
	bad.api_key = "not-used";
	bad.video = "https://example.invalid/watch?v=abcDEF12345";
	CheckRestartCycle(CreateYouTubeProvider(bad, &hub), hub, sub, "Not a YouTube video ID or URL");
}

TEST_CASE(provider_destructor_stops_running_provider)
{
	ChatHub hub;
	{
		auto p = CreateTwitchProvider({"#"}, &hub);
		p->Start();
	} // destructor joins the worker
	CHECK(true);
}
