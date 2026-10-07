// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "emotes.h"
#include "parse-kick.h"
#include "parse-twitch.h"

using namespace tandem::chat;

TEST_CASE(twitch_emote_tag_ascii)
{
	// "Kappa hello Kappa" with Kappa (id 25) at 0-4 and 12-16
	auto spans = ParseTwitchEmotes("25:0-4,12-16", "Kappa hello Kappa");
	CHECK_EQ(spans.size(), (size_t)2);
	CHECK_EQ(spans[0].begin, (size_t)0);
	CHECK_EQ(spans[0].end, (size_t)5);
	CHECK_EQ(spans[0].name, std::string("Kappa"));
	CHECK_EQ(spans[1].begin, (size_t)12);
	CHECK_EQ(spans[0].source, std::string("twitch"));
	CHECK(spans[0].url.find("/emoticons/v2/25/") != std::string::npos);
}

TEST_CASE(twitch_emote_tag_counts_code_points)
{
	// An emoji (4 UTF-8 bytes, 1 code point) before the emote shifts byte offsets.
	std::string text = "\xF0\x9F\x98\x80 Kappa";
	auto spans = ParseTwitchEmotes("25:2-6", text);
	CHECK_EQ(spans.size(), (size_t)1);
	CHECK_EQ(spans[0].begin, (size_t)5);
	CHECK_EQ(spans[0].end, (size_t)10);
	CHECK_EQ(spans[0].name, std::string("Kappa"));
}

TEST_CASE(twitch_emote_tag_malformed)
{
	CHECK(ParseTwitchEmotes("", "Kappa").empty());
	CHECK(ParseTwitchEmotes("25:0-99", "Kappa").empty());      // out of range
	CHECK(ParseTwitchEmotes("25:4-1", "Kappa").empty());       // reversed
	CHECK(ParseTwitchEmotes("a.b:0-4", "Kappa").empty());      // unsafe id
	CHECK_EQ(ParseTwitchEmotes("25:0-4/26:2-3", "Kappa").size(), (size_t)1); // overlap dropped
	auto ev = ParseTwitchLine("@emotes=25:0-4;room-id=123;id=a;user-id=1;display-name=V :v!v@v PRIVMSG #c :Kappa hi", 1);
	CHECK_EQ(ev.message.emotes.size(), (size_t)1);
	CHECK_EQ(ev.room_id, std::string("123"));
}

TEST_CASE(kick_emote_spans)
{
	std::vector<EmoteSpan> spans;
	std::string out = StripKickEmotes("hi [emote:37226:KEKW] and [emote:1:x]!", &spans);
	CHECK_EQ(out, std::string("hi KEKW and x!"));
	CHECK_EQ(spans.size(), (size_t)2);
	CHECK_EQ(out.substr(spans[0].begin, spans[0].end - spans[0].begin), std::string("KEKW"));
	CHECK_EQ(spans[0].url, std::string("https://files.kick.com/emotes/37226/fullsize"));
	CHECK_EQ(out.substr(spans[1].begin, spans[1].end - spans[1].begin), std::string("x"));
	CHECK_EQ(ParseKickChannelUserId(R"({"id":668,"user_id":676,"chatroom":{"id":668}})"), std::string("676"));
	CHECK(ParseKickChannelUserId("not json").empty());
}

TEST_CASE(third_party_emote_parsers)
{
	auto stv = Parse7tvUser(R"({"emote_set":{"emotes":[{"id":"01ABC","name":"GAMBA"},{"id":"bad id","name":"x"},{"name":"noid"}]}})");
	CHECK_EQ(stv.size(), (size_t)1);
	CHECK_EQ(stv["GAMBA"].url, std::string("https://cdn.7tv.app/emote/01ABC/2x.webp"));
	CHECK_EQ(Parse7tvEmoteSet(R"({"emotes":[{"id":"1","name":"EZ"}]})").size(), (size_t)1);

	auto bttv = ParseBttvUser(R"({"channelEmotes":[{"id":"a1","code":"catJAM"}],"sharedEmotes":[{"id":"b2","code":"pepeD"}]})");
	CHECK_EQ(bttv.size(), (size_t)2);
	CHECK_EQ(bttv["pepeD"].url, std::string("https://cdn.betterttv.net/emote/b2/2x"));
	CHECK_EQ(ParseBttvEmotes(R"([{"id":"c3","code":":tf:"}])").size(), (size_t)1);

	auto ffz = ParseFfzRoom(R"({"sets":{"9":{"emoticons":[{"id":246878,"name":"WideHard","urls":{"1":"u","2":"u"}}]}}})");
	CHECK_EQ(ffz["WideHard"].url, std::string("https://cdn.frankerfacez.com/emote/246878/2"));
	auto ffzg = ParseFfzGlobal(R"({"default_sets":[3],"sets":{"3":{"emoticons":[{"id":1,"name":"ZreknarF","urls":{"1":"u"}}]},"4":{"emoticons":[{"id":2,"name":"Hidden","urls":{"1":"u"}}]}}})");
	CHECK_EQ(ffzg.size(), (size_t)1);
	CHECK_EQ(ffzg["ZreknarF"].url, std::string("https://cdn.frankerfacez.com/emote/1/1"));

	CHECK(Parse7tvUser("").empty());
	CHECK(ParseBttvUser("{").empty());
	CHECK(ParseFfzGlobal("[]").empty());
}

TEST_CASE(annotate_words_skips_native_and_partial_words)
{
	EmoteMap map;
	map["GAMBA"] = {"https://cdn.7tv.app/emote/1/2x.webp", "7tv"};
	map["Kappa"] = {"https://cdn.7tv.app/emote/2/2x.webp", "7tv"};
	ChatMessage m;
	m.text = "Kappa GAMBA GAMBAx  GAMBA";
	EmoteSpan native;
	native.begin = 0;
	native.end = 5;
	native.name = "Kappa";
	native.url = "https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0";
	native.source = "twitch";
	m.emotes.push_back(native);
	AnnotateWords(m, map);
	CHECK_EQ(m.emotes.size(), (size_t)3);
	CHECK_EQ(m.emotes[0].source, std::string("twitch")); // native kept, not replaced
	CHECK_EQ(m.emotes[1].begin, (size_t)6);
	CHECK_EQ(m.emotes[2].begin, (size_t)20);
}

TEST_CASE(trusted_emote_urls)
{
	CHECK(IsTrustedEmoteUrl("https://cdn.7tv.app/emote/1/2x.webp"));
	CHECK(IsTrustedEmoteUrl("https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0"));
	CHECK(!IsTrustedEmoteUrl("http://cdn.7tv.app/emote/1/2x.webp"));
	CHECK(!IsTrustedEmoteUrl("https://cdn.7tv.app.evil.example/x"));
	CHECK(!IsTrustedEmoteUrl("https://evil.example/https://cdn.7tv.app/"));
	CHECK(!IsTrustedEmoteUrl("https://cdn.7tv.app/a\"onerror=x"));
	CHECK(!IsTrustedEmoteUrl("https://cdn.7tv.app/"));
}
