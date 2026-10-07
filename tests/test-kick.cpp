// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "parse-kick.h"

#include <json.hpp>

using namespace tandem::chat;
using Ty = KickEvent::Type;

namespace {
constexpr int64_t kNow = 777;

std::vector<std::string> Lines(const std::string &s)
{
	std::vector<std::string> out;
	size_t p = 0;
	while (p < s.size()) {
		size_t nl = s.find('\n', p);
		std::string line = s.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		out.push_back(line);
		if (nl == std::string::npos)
			break;
		p = nl + 1;
	}
	return out;
}
} // namespace

TEST_CASE(kick_emote_stripping)
{
	CHECK_EQ(StripKickEmotes("gg [emote:37226:KEKW] nice"), std::string("gg KEKW nice"));
	CHECK_EQ(StripKickEmotes("[emote:1:a][emote:2:b]"), std::string("ab"));
	CHECK_EQ(StripKickEmotes("no emotes"), std::string("no emotes"));
	CHECK_EQ(StripKickEmotes("[emote:abc:Name]"), std::string("[emote:abc:Name]"));
	CHECK_EQ(StripKickEmotes("[emote:123:]"), std::string("[emote:123:]"));
	CHECK_EQ(StripKickEmotes("[emote:123:Name"), std::string("[emote:123:Name"));
	CHECK_EQ(StripKickEmotes("[emote:12[emote:5:ok]"), std::string("[emote:12ok"));
	CHECK_EQ(StripKickEmotes("[emote:"), std::string("[emote:"));
	CHECK_EQ(StripKickEmotes(""), std::string());
	CHECK_EQ(StripKickEmotes("a [emote:9:Name:With:Colons] b"), std::string("a Name:With:Colons b"));
}

TEST_CASE(kick_slug_and_id_validation)
{
	CHECK(IsValidKickSlug("sample-channel_1"));
	CHECK(IsValidKickSlug("A"));
	CHECK(!IsValidKickSlug(""));
	CHECK(!IsValidKickSlug("bad slug"));
	CHECK(!IsValidKickSlug("../etc"));
	CHECK(!IsValidKickSlug(std::string(65, 'a')));
	CHECK(IsValidKickChatroomId("4242"));
	CHECK(!IsValidKickChatroomId("0"));
	CHECK(!IsValidKickChatroomId("42a"));
	CHECK(!IsValidKickChatroomId(""));
	CHECK(!IsValidKickChatroomId("-1"));
}

TEST_CASE(kick_channel_lookup)
{
	CHECK_EQ(ParseKickChannelChatroomId(tt::ReadFixture("kick-channel.json")).value_or(""), std::string("4242"));
	CHECK(!ParseKickChannelChatroomId("<html>Just a moment...</html>"));
	CHECK(!ParseKickChannelChatroomId("{}"));
	CHECK(!ParseKickChannelChatroomId(R"({"chatroom":null})"));
	CHECK_EQ(ParseKickChannelChatroomId(R"({"chatroom":{"id":"99"}})").value_or(""), std::string("99"));
}

TEST_CASE(kick_outgoing_messages)
{
	auto j = nlohmann::json::parse(KickSubscribeMessage("4242"));
	CHECK_EQ(j["event"].get<std::string>(), std::string("pusher:subscribe"));
	CHECK_EQ(j["data"]["channel"].get<std::string>(), std::string("chatrooms.4242.v2"));
	CHECK_EQ(j["data"]["auth"].get<std::string>(), std::string());
	CHECK(nlohmann::json::parse(KickPongMessage())["event"] == "pusher:pong");
	CHECK(nlohmann::json::parse(KickPingMessage())["event"] == "pusher:ping");
}

TEST_CASE(kick_events_fixture)
{
	auto lines = Lines(tt::ReadFixture("kick-events.jsonl"));
	CHECK_EQ(lines.size(), size_t(16));
	if (lines.size() < 16)
		return;
	std::vector<KickEvent> evs;
	for (const auto &l : lines)
		evs.push_back(ParseKickPusher(l, kNow));

	CHECK_EQ(evs[0].type, Ty::ConnectionEstablished);
	CHECK_EQ(evs[0].activity_timeout_s, 120);
	CHECK_EQ(evs[1].type, Ty::SubscriptionSucceeded);
	CHECK_EQ(evs[1].channel, std::string("chatrooms.4242.v2"));

	const auto &m = evs[2];
	CHECK_EQ(m.type, Ty::Message);
	CHECK_EQ(m.message.id, std::string("9b6c1f0e-1111-4c2d-8e3f-000000000001"));
	CHECK_EQ(m.message.author, std::string("SampleViewer"));
	CHECK_EQ(m.message.author_id, std::string("5550001"));
	CHECK_EQ(m.message.color, std::string("#E9113C"));
	CHECK_EQ(m.message.text, std::string("gg KEKW nice x"));
	CHECK_EQ(m.message.timestamp_ms, 1791397815000LL);
	CHECK(m.message.badges == std::vector<std::string>({"moderator", "subscriber"}));
	CHECK(m.message.platform == Platform::Kick);

	const auto &m2 = evs[3]; // data as a plain object, with unknown extra fields
	CHECK_EQ(m2.type, Ty::Message);
	CHECK_EQ(m2.message.text, std::string("plain object payload"));
	CHECK_EQ(m2.message.timestamp_ms, 1791397816250LL);
	CHECK(m2.message.badges.empty());

	CHECK_EQ(evs[4].type, Ty::DeleteMessage);
	CHECK_EQ(evs[4].target_id, std::string("9b6c1f0e-1111-4c2d-8e3f-000000000001"));
	CHECK_EQ(evs[5].type, Ty::ClearUser);
	CHECK_EQ(evs[5].target_id, std::string("5550002"));
	CHECK_EQ(evs[6].type, Ty::ClearAll);
	CHECK_EQ(evs[7].type, Ty::Unknown);
	CHECK_EQ(evs[8].type, Ty::Malformed); // truncated JSON
	CHECK_EQ(evs[9].type, Ty::Malformed); // data string is not JSON
	CHECK_EQ(evs[9].event_name, std::string("App\\Events\\ChatMessageEvent"));
	CHECK_EQ(evs[10].type, Ty::Malformed); // content is a number
	CHECK_EQ(evs[11].type, Ty::Ping);
	CHECK_EQ(evs[12].type, Ty::PusherError);
	CHECK_EQ(evs[12].error_code, 4201);
	CHECK_EQ(evs[13].type, Ty::PusherError);
	CHECK_EQ(evs[13].error_code, 0);

	const auto &m3 = evs[14]; // bad timestamp and no sender
	CHECK_EQ(m3.type, Ty::Message);
	CHECK_EQ(m3.message.timestamp_ms, kNow);
	CHECK_EQ(m3.message.author, std::string());
	CHECK_EQ(evs[15].type, Ty::Malformed); // JSON array, not an object
}

TEST_CASE(kick_garbage_never_throws)
{
	const char *junk[] = {"", "null", "{}", "[]", "\"str\"", "{\"event\":5}", "{\"event\":\"pusher:connection_established\"}",
			      "{\"event\":\"pusher:connection_established\",\"data\":\"{\\\"activity_timeout\\\":\\\"x\\\"}\"}",
			      "{\"event\":\"App\\\\Events\\\\ChatMessageEvent\",\"data\":null}",
			      "{\"event\":\"App\\\\Events\\\\ChatMessageEvent\",\"data\":\"[]\"}",
			      "{\"event\":\"App\\\\Events\\\\ChatMessageEvent\",\"data\":{\"content\":\"x\",\"sender\":[],\"id\":{}}}",
			      "{\"event\":\"App\\\\Events\\\\MessageDeletedEvent\",\"data\":{\"message\":5}}",
			      "{\"event\":\"App\\\\Events\\\\UserBannedEvent\",\"data\":\"{}\"}",
			      "{\"event\":\"pusher:error\",\"data\":\"oops\"}"};
	for (const char *j : junk) {
		auto ev = ParseKickPusher(j, kNow);
		(void)ev;
	}
	auto est = ParseKickPusher("{\"event\":\"pusher:connection_established\"}", kNow);
	CHECK_EQ(est.type, Ty::ConnectionEstablished);
	CHECK_EQ(est.activity_timeout_s, 120);
	CHECK_EQ(ParseKickPusher("{\"event\":\"App\\\\Events\\\\UserBannedEvent\",\"data\":\"{}\"}", kNow).type,
		 Ty::Malformed);
}
