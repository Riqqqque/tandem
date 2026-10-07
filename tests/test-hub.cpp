// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "chat-hub.h"

#include <thread>

using namespace tandem::chat;

namespace {
ChatEvent Msg(Platform p, const std::string &id, const std::string &author_id = "u1")
{
	ChatEvent ev;
	ev.kind = ChatEvent::Kind::Message;
	ev.platform = p;
	ev.message.platform = p;
	ev.message.id = id;
	ev.message.author_id = author_id;
	ev.message.text = "text " + id;
	return ev;
}

ChatEvent Ctl(ChatEvent::Kind kind, Platform p, const std::string &target = {})
{
	ChatEvent ev;
	ev.kind = kind;
	ev.platform = p;
	ev.target_id = target;
	if (kind == ChatEvent::Kind::Status)
		ev.status = {ProviderState::Connected, target};
	return ev;
}
} // namespace

TEST_CASE(hub_fan_out_to_all_subscribers)
{
	ChatHub hub;
	int a = hub.Subscribe();
	int b = hub.Subscribe();
	CHECK(a != b);
	hub.Post(Msg(Platform::Twitch, "1"));
	hub.Post(Msg(Platform::Kick, "2"));
	std::vector<ChatEvent> ea, eb;
	CHECK_EQ(hub.Drain(a, ea), size_t(2));
	CHECK_EQ(hub.Drain(b, eb), size_t(2));
	CHECK_EQ(ea[1].message.id, std::string("2"));
	CHECK_EQ(eb[0].message.id, std::string("1"));
	// Draining one subscriber does not affect the other.
	hub.Post(Msg(Platform::Twitch, "3"));
	ea.clear();
	CHECK_EQ(hub.Drain(a, ea, 1), size_t(1));
	hub.Unsubscribe(a);
	CHECK_EQ(hub.Drain(a, ea), size_t(0));
	eb.clear();
	CHECK_EQ(hub.Drain(b, eb), size_t(1));
}

TEST_CASE(hub_bounded_queue_drops_oldest_messages_keeps_control)
{
	ChatHub hub(5, 100);
	int s = hub.Subscribe();
	hub.Post(Ctl(ChatEvent::Kind::Status, Platform::Twitch, "s1"));
	for (int i = 0; i < 10; ++i)
		hub.Post(Msg(Platform::Twitch, std::to_string(i)));
	std::vector<ChatEvent> out;
	hub.Drain(s, out);
	CHECK_EQ(out.size(), size_t(5));
	CHECK(out[0].kind == ChatEvent::Kind::Status); // control event survived
	CHECK_EQ(out.back().message.id, std::string("9"));
	CHECK_EQ(out[1].message.id, std::string("6"));
	CHECK_EQ(hub.dropped(), size_t(6));

	// A queue full of control events drops new chat messages but still accepts control events.
	ChatHub ctl(3, 10);
	int c = ctl.Subscribe();
	for (int i = 0; i < 3; ++i)
		ctl.Post(Ctl(ChatEvent::Kind::DeleteMessage, Platform::Kick, std::to_string(i)));
	ctl.Post(Msg(Platform::Kick, "m"));
	ctl.Post(Ctl(ChatEvent::Kind::ClearAll, Platform::Kick));
	out.clear();
	ctl.Drain(c, out);
	CHECK_EQ(out.size(), size_t(4));
	CHECK(out.back().kind == ChatEvent::Kind::ClearAll);
	CHECK_EQ(ctl.dropped(), size_t(1));
}

TEST_CASE(hub_history_deletes)
{
	ChatHub hub(100, 4);
	hub.Post(Msg(Platform::Twitch, "a", "u1"));
	hub.Post(Msg(Platform::Kick, "a", "u1"));
	hub.Post(Msg(Platform::Twitch, "b", "u2"));
	hub.Post(Msg(Platform::Twitch, "c", "u1"));
	hub.Post(Msg(Platform::YouTube, "d", "u1"));
	auto h = hub.History();
	CHECK_EQ(h.size(), size_t(4)); // limited, oldest dropped
	CHECK_EQ(h.front().platform, Platform::Kick);

	hub.Post(Ctl(ChatEvent::Kind::DeleteMessage, Platform::Twitch, "c"));
	h = hub.History();
	CHECK_EQ(h.size(), size_t(3));

	// ClearUser only touches that platform.
	hub.Post(Ctl(ChatEvent::Kind::ClearUser, Platform::Kick, "u1"));
	h = hub.History();
	CHECK_EQ(h.size(), size_t(2));
	for (const auto &m : h)
		CHECK(m.platform != Platform::Kick);

	hub.Post(Ctl(ChatEvent::Kind::ClearAll, Platform::Twitch));
	h = hub.History();
	CHECK_EQ(h.size(), size_t(1));
	CHECK_EQ(h[0].platform, Platform::YouTube);

	hub.ClearHistory();
	CHECK(hub.History().empty());
}

TEST_CASE(hub_status_and_wait)
{
	ChatHub hub;
	int s = hub.Subscribe();
	CHECK(hub.Status(Platform::Kick).state == ProviderState::Stopped);
	hub.Post(Ctl(ChatEvent::Kind::Status, Platform::Kick, "chatroom 1"));
	CHECK(hub.Status(Platform::Kick).state == ProviderState::Connected);
	CHECK_EQ(hub.Status(Platform::Kick).detail, std::string("chatroom 1"));
	CHECK(hub.Status(Platform::Twitch).state == ProviderState::Stopped);

	CHECK(hub.Wait(s, std::chrono::milliseconds(10))); // already has an event
	std::vector<ChatEvent> out;
	hub.Drain(s, out);
	CHECK(!hub.Wait(s, std::chrono::milliseconds(10)));

	std::thread t([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		hub.WakeAll();
	});
	CHECK(hub.Wait(s, std::chrono::seconds(5)));
	t.join();

	// Many concurrent producers.
	std::vector<std::thread> producers;
	for (int p = 0; p < 4; ++p) {
		producers.emplace_back([&hub, p] {
			for (int i = 0; i < 250; ++i)
				hub.Post(Msg(static_cast<Platform>(p % 3), std::to_string(p * 1000 + i)));
		});
	}
	for (auto &th : producers)
		th.join();
	out.clear();
	hub.Drain(s, out);
	CHECK_EQ(out.size(), size_t(1000));
}
