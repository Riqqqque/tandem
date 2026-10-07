// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - manual live check for the chat providers.
//   tandem-chat-probe twitch <channel> [seconds]
//   tandem-chat-probe kick <slug> [chatroomId] [seconds]
//   tandem-chat-probe youtube <video id or URL> [seconds]   (key from env TANDEM_YT_KEY)
#include "chat-hub.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace tandem::chat;

namespace {

void PrintLog(LogLevel level, const char *msg)
{
	static const char *names[] = {"debug", "info", "warn", "error"};
	std::fprintf(stderr, "[%s] %s\n", names[static_cast<int>(level)], msg);
}

bool IsNumber(const char *s)
{
	if (!*s)
		return false;
	for (; *s; ++s) {
		if (*s < '0' || *s > '9')
			return false;
	}
	return true;
}

int Usage()
{
	std::fprintf(stderr, "usage: tandem-chat-probe twitch <channel> [seconds]\n"
			     "       tandem-chat-probe kick <slug> [chatroomId] [seconds]\n"
			     "       tandem-chat-probe youtube <video> [seconds]   (API key from TANDEM_YT_KEY)\n");
	return 2;
}

} // namespace

int main(int argc, char **argv)
{
	SetLogFunction(PrintLog);
	if (argc < 3)
		return Usage();
	std::string platform = argv[1];
	int seconds = 30;
	ChatHub hub(5000, 50);
	std::unique_ptr<ChatProvider> provider;

	if (platform == "twitch") {
		if (argc > 3)
			seconds = std::atoi(argv[3]);
		provider = CreateTwitchProvider({argv[2]}, &hub);
	} else if (platform == "kick") {
		KickChatConfig cfg;
		cfg.channel = argv[2];
		int next = 3;
		if (argc > 4 || (argc > 3 && std::strlen(argv[3]) > 4 && IsNumber(argv[3]))) {
			cfg.chatroom_id = argv[3];
			next = 4;
		}
		if (argc > next)
			seconds = std::atoi(argv[next]);
		provider = CreateKickProvider(cfg, &hub);
	} else if (platform == "youtube") {
		YouTubeChatConfig cfg;
		const char *key = std::getenv("TANDEM_YT_KEY");
		cfg.api_key = key ? key : "";
		cfg.video = argv[2];
		if (argc > 3)
			seconds = std::atoi(argv[3]);
		cfg.on_quota = [](int64_t used) { std::fprintf(stderr, "[quota] %lld units\n", (long long)used); };
		provider = CreateYouTubeProvider(cfg, &hub);
	} else {
		return Usage();
	}
	if (seconds <= 0)
		seconds = 30;

	int sub = hub.Subscribe();
	long long messages = 0, deletes = 0, clears = 0, statuses = 0, errors = 0, reconnects = 0;
	auto t0 = std::chrono::steady_clock::now();
	provider->Start();
	auto deadline = t0 + std::chrono::seconds(seconds);
	std::vector<ChatEvent> events;
	auto print = [&](const ChatEvent &ev) {
		double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		switch (ev.kind) {
		case ChatEvent::Kind::Message: {
			++messages;
			std::string badges;
			for (const auto &b : ev.message.badges)
				badges += (badges.empty() ? "" : ",") + b;
			std::printf("%7.2f %-7s %s%s%s: %s\n", t, PlatformKey(ev.platform), ev.message.author.c_str(),
				    badges.empty() ? "" : " [", badges.empty() ? "" : (badges + "]").c_str(),
				    ev.message.text.c_str());
			break;
		}
		case ChatEvent::Kind::DeleteMessage:
			++deletes;
			std::printf("%7.2f %-7s <delete %s>\n", t, PlatformKey(ev.platform), ev.target_id.c_str());
			break;
		case ChatEvent::Kind::ClearUser:
		case ChatEvent::Kind::ClearAll:
			++clears;
			std::printf("%7.2f %-7s <clear %s>\n", t, PlatformKey(ev.platform), ev.target_id.c_str());
			break;
		case ChatEvent::Kind::Status:
			++statuses;
			if (ev.status.state == ProviderState::Error)
				++errors;
			if (ev.status.state == ProviderState::Reconnecting)
				++reconnects;
			std::printf("%7.2f %-7s STATUS %s %s\n", t, PlatformKey(ev.platform),
				    ProviderStateName(ev.status.state), ev.status.detail.c_str());
			break;
		case ChatEvent::Kind::Quota:
			std::printf("%7.2f %-7s QUOTA %lld\n", t, PlatformKey(ev.platform), (long long)ev.quota_used);
			break;
		}
		std::fflush(stdout);
	};
	while (std::chrono::steady_clock::now() < deadline) {
		hub.Wait(sub, std::chrono::milliseconds(200));
		events.clear();
		hub.Drain(sub, events);
		for (const auto &ev : events)
			print(ev);
	}
	auto stop_begin = std::chrono::steady_clock::now();
	provider->Stop();
	double stop_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - stop_begin).count();
	events.clear();
	hub.Drain(sub, events);
	for (const auto &ev : events)
		print(ev);
	std::printf("\nSUMMARY %s: %lld messages, %lld deletes, %lld clears, %lld status events (%lld reconnecting, "
		    "%lld error), stop took %.2fs\n",
		    platform.c_str(), messages, deletes, clears, statuses, reconnects, errors, stop_s);
	return 0;
}
