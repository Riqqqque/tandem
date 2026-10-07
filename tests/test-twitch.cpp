// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "parse-twitch.h"

#include <map>

using namespace tandem::chat;
using Ty = TwitchEvent::Type;

namespace {
std::vector<std::string> Lines(const std::string &s)
{
	std::vector<std::string> out;
	size_t p = 0;
	while (p <= s.size()) {
		size_t nl = s.find('\n', p);
		if (nl == std::string::npos) {
			if (p < s.size())
				out.push_back(s.substr(p));
			break;
		}
		out.push_back(s.substr(p, nl - p));
		p = nl + 1;
	}
	return out;
}
constexpr int64_t kNow = 1234;
} // namespace

TEST_CASE(irc_tag_unescape)
{
	CHECK_EQ(UnescapeIrcTagValue("hi\\sthere\\:\\\\you\\nnext\\r"), std::string("hi there;\\you\nnext\r"));
	CHECK_EQ(UnescapeIrcTagValue("plain"), std::string("plain"));
	CHECK_EQ(UnescapeIrcTagValue("unknown\\x"), std::string("unknownx"));
	CHECK_EQ(UnescapeIrcTagValue("trailing\\"), std::string("trailing"));
	CHECK_EQ(UnescapeIrcTagValue(""), std::string());
}

TEST_CASE(irc_line_split)
{
	auto l = ParseIrcLine("@a=1;b=;c :nick!user@host CMD p1 p2 :trailing with : colon\r\n");
	CHECK(l.has_value());
	if (!l)
		return;
	CHECK_EQ(l->tags.size(), size_t(3));
	CHECK_EQ(*l->Tag("a"), std::string("1"));
	CHECK_EQ(*l->Tag("b"), std::string());
	CHECK_EQ(*l->Tag("c"), std::string());
	CHECK(l->Tag("d") == nullptr);
	CHECK_EQ(l->nick, std::string("nick"));
	CHECK_EQ(l->command, std::string("CMD"));
	CHECK_EQ(l->params.size(), size_t(3));
	CHECK_EQ(l->params[2], std::string("trailing with : colon"));

	CHECK(!ParseIrcLine(""));
	CHECK(!ParseIrcLine("@onlytags"));
	CHECK(!ParseIrcLine(":prefixonly"));
	CHECK(!ParseIrcLine("\r\n"));
}

TEST_CASE(twitch_badges)
{
	auto b = MapTwitchBadges("broadcaster/1,moderator/1,subscriber/12,vip/1,founder/0,partner/1,glhf-pledge/1");
	std::vector<std::string> want = {"broadcaster", "moderator", "subscriber", "vip", "verified"};
	CHECK(b == want);
	CHECK(MapTwitchBadges("").empty());
	CHECK(MapTwitchBadges(",,/,").empty());
}

TEST_CASE(twitch_channel_validation)
{
	CHECK_EQ(NormalizeTwitchChannel("  #SampleChannel "), std::string("samplechannel"));
	CHECK(IsValidTwitchChannel("samplechannel"));
	CHECK(IsValidTwitchChannel("abcd"));
	CHECK(IsValidTwitchChannel("a_1234567890123456789012b")); // 25
	CHECK(IsValidTwitchChannel("xqc")); // short legacy names exist
	CHECK(!IsValidTwitchChannel(""));
	CHECK(!IsValidTwitchChannel("a_1234567890123456789012bc")); // 26
	CHECK(!IsValidTwitchChannel("bad name"));
	CHECK(!IsValidTwitchChannel("bad-name"));
	CHECK(!IsValidTwitchChannel(NormalizeTwitchChannel("#")));
	CHECK(!IsValidTwitchChannel("caf\xc3\xa9x"));
}

TEST_CASE(twitch_action_message)
{
	auto ev = ParseTwitchLine("@display-name=Waver;id=m1;user-id=7 :waver!waver@waver.tmi.twitch.tv PRIVMSG #chan "
				  ":\x01" "ACTION waves at everyone\x01",
				  kNow);
	CHECK_EQ(ev.type, Ty::Message);
	CHECK(ev.action);
	CHECK_EQ(ev.message.text, std::string("waves at everyone"));
	CHECK_EQ(ev.message.timestamp_ms, kNow);

	// Missing trailing \x01 still strips the prefix.
	auto ev2 = ParseTwitchLine(":w!w@w PRIVMSG #chan :\x01" "ACTION dances", kNow);
	CHECK_EQ(ev2.message.text, std::string("dances"));
	CHECK_EQ(ev2.message.author, std::string("w"));
}

TEST_CASE(twitch_session_fixture)
{
	auto lines = Lines(tt::ReadFixture("twitch-session.txt"));
	CHECK(lines.size() >= 29);
	std::vector<TwitchEvent> evs;
	for (const auto &l : lines)
		evs.push_back(ParseTwitchLine(l, kNow));
	if (evs.size() < 29)
		return;

	CHECK_EQ(evs[0].type, Ty::Welcome);
	CHECK_EQ(evs[1].type, Ty::None);
	CHECK_EQ(evs[4].type, Ty::None); // CAP ACK
	CHECK_EQ(evs[5].type, Ty::Join);
	CHECK_EQ(evs[5].channel, std::string("samplechannel"));
	CHECK_EQ(evs[5].nick, std::string("justinfan12345"));
	CHECK_EQ(evs[8].type, Ty::RoomState);

	const auto &m1 = evs[9];
	CHECK_EQ(m1.type, Ty::Message);
	CHECK_EQ(m1.message.author, std::string("SampleChannel"));
	CHECK_EQ(m1.message.author_id, std::string("100000001"));
	CHECK_EQ(m1.message.color, std::string("#1E90FF"));
	CHECK_EQ(m1.message.id, std::string("3f2c8e4a-0001-4a5b-9c1d-000000000001"));
	CHECK_EQ(m1.message.text, std::string("Hello chat; welcome in"));
	CHECK_EQ(m1.message.timestamp_ms, 1791405015123LL);
	CHECK(m1.message.badges == std::vector<std::string>({"broadcaster", "subscriber"}));
	CHECK(!m1.action);

	const auto &m2 = evs[10];
	CHECK_EQ(m2.type, Ty::Message);
	CHECK_EQ(m2.message.author, std::string("fallbacknick")); // empty display-name
	CHECK_EQ(m2.message.color, std::string());
	CHECK_EQ(m2.message.text, std::string("no display name here :) with: colons"));
	CHECK(m2.message.badges == std::vector<std::string>({"moderator", "vip", "verified"}));

	const auto &m3 = evs[11];
	CHECK_EQ(m3.type, Ty::Message);
	CHECK_EQ(m3.message.author, std::string("Viewer_One"));
	CHECK_EQ(m3.message.timestamp_ms, kNow); // tmi-sent-ts not a number
	auto l3 = ParseIrcLine(lines[11]);
	CHECK(l3 && l3->Tag("reply-parent-msg-body") &&
	      *l3->Tag("reply-parent-msg-body") == std::string("hi there;\\you\nnext"));

	CHECK_EQ(evs[12].type, Ty::DeleteMessage);
	CHECK_EQ(evs[12].target_id, std::string("3f2c8e4a-0001-4a5b-9c1d-000000000003"));
	CHECK_EQ(evs[13].type, Ty::ClearUser);
	CHECK_EQ(evs[13].target_id, std::string("100000003"));
	CHECK_EQ(evs[13].target_login, std::string("viewer_one"));
	CHECK_EQ(evs[14].type, Ty::ClearAll);
	CHECK_EQ(evs[15].type, Ty::None); // USERNOTICE is not shown
	CHECK_EQ(evs[16].type, Ty::Ping);
	CHECK_EQ(evs[16].param, std::string("tmi.twitch.tv"));
	CHECK_EQ(evs[17].type, Ty::Pong);
	CHECK_EQ(evs[17].param, std::string("tandem"));
	CHECK_EQ(evs[18].type, Ty::Reconnect);

	CHECK_EQ(evs[19].type, Ty::Notice);
	CHECK_EQ(evs[19].notice_id, std::string("msg_channel_suspended"));
	CHECK(IsPermanentTwitchNotice(evs[19]));
	CHECK_EQ(evs[20].type, Ty::Notice);
	CHECK(IsPermanentTwitchNotice(evs[20])); // login failure, untagged
	CHECK_EQ(evs[21].type, Ty::Notice);
	CHECK(!IsPermanentTwitchNotice(evs[21])); // emote-only is informational

	// Malformed lines never produce events.
	for (size_t i = 22; i < evs.size(); ++i)
		CHECK_EQ(evs[i].type, Ty::None);
}

TEST_CASE(twitch_garbage_never_throws)
{
	const char *junk[] = {"@", "@;;;=;= :", ": :", "PRIVMSG", "PRIVMSG #", "PRIVMSG # :", "@id=\\ :a!b@c PRIVMSG #x :",
			      "\x01\x02\x03", "CLEARCHAT", "CLEARMSG #a", "JOIN", ":a JOIN #", "PING"};
	for (const char *j : junk) {
		auto ev = ParseTwitchLine(j, kNow);
		(void)ev;
	}
	auto ping = ParseTwitchLine("PING", kNow);
	CHECK_EQ(ping.type, Ty::Ping);
	CHECK_EQ(ping.param, std::string());
	CHECK_EQ(ParseTwitchLine("CLEARMSG #a", kNow).type, Ty::None); // no target id
	std::string big(100000, 'x');
	auto ev = ParseTwitchLine(":a!a@a PRIVMSG #chan :" + big, kNow);
	CHECK_EQ(ev.message.text.size(), big.size());
}
