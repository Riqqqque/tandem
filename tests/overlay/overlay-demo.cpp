// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - manual overlay demo: serves 127.0.0.1:<port> and posts fake chat from all three
// platforms every 500 ms. Usage: overlay-demo [port=48080] [seconds=60]
#include "overlay-server.h"

#include "chat-hub.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace tandem;

int main(int argc, char **argv)
{
	int port = argc > 1 ? std::atoi(argv[1]) : 48080;
	int seconds = argc > 2 ? std::atoi(argv[2]) : 60;

	chat::ChatHub hub;
	overlay::OverlayServer srv;
	overlay::OverlayOptions opt;
	opt.port = port;
	std::string err;
	if (!srv.Start(&hub, opt, &err)) {
		fprintf(stderr, "start failed: %s\n", err.c_str());
		return 1;
	}
	printf("open http://127.0.0.1:%d/?max=10&fade=0\n", srv.port());
	fflush(stdout);

	struct Fake {
		chat::Platform p;
		const char *author;
		const char *color;
		const char *badge;
		const char *text;
	};
	static const Fake fakes[] = {
		{chat::Platform::Twitch, "NightOwl", "#9146FF", "subscriber", "hello from twitch!"},
		{chat::Platform::Kick, "GreenRunner", "#53FC18", "", "kick chat checking in"},
		{chat::Platform::YouTube, "Ana Souza", "", "member", "great stream \xF0\x9F\x94\xA5\xF0\x9F\x98\x80"},
		{chat::Platform::Twitch, "xss_tester", "#FF0000", "", "<script>alert(1)</script> <b>not bold</b> &amp;"},
		{chat::Platform::Kick, "DarkName", "#0A0A20", "moderator", "my name color is very dark"},
		{chat::Platform::YouTube, "Streamer", "#FFD700", "owner", "thanks for watching \xE2\x9D\xA4\xEF\xB8\x8F"},
		{chat::Platform::Twitch, "LongTalker", "#1E90FF", "vip",
		 "this is a rather long message to check that wrapping works nicely inside the overlay box without "
		 "overflowing the browser source width at all"},
	};

	auto start = std::chrono::steady_clock::now();
	int n = 0;
	while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
		const Fake &f = fakes[n % (sizeof(fakes) / sizeof(fakes[0]))];
		chat::ChatEvent ev;
		ev.kind = chat::ChatEvent::Kind::Message;
		ev.platform = f.p;
		ev.message.platform = f.p;
		ev.message.id = "demo-" + std::to_string(n);
		ev.message.author = f.author;
		ev.message.author_id = std::string("uid-") + f.author;
		ev.message.color = f.color;
		if (*f.badge)
			ev.message.badges.push_back(f.badge);
		ev.message.text = std::string(f.text) + " #" + std::to_string(n);
		ev.message.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						  std::chrono::system_clock::now().time_since_epoch())
						  .count();
		hub.Post(std::move(ev));
		++n;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	srv.Stop();
	return 0;
}
