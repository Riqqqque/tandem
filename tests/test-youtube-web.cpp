// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "parse-youtube-web.h"

using namespace tandem::chat;
using Kind = YouTubeTarget::Kind;

TEST_CASE(youtube_web_target_channels)
{
	struct Case {
		const char *in;
		const char *path;
	} cases[] = {
		{"@LofiGirl", "@LofiGirl"},
		{"  @LofiGirl  ", "@LofiGirl"},
		{"LofiGirl", "@LofiGirl"},
		{"https://www.youtube.com/@LofiGirl", "@LofiGirl"},
		{"https://www.youtube.com/@LofiGirl/live", "@LofiGirl"},
		{"youtube.com/@LofiGirl/streams?si=x", "@LofiGirl"},
		{"m.youtube.com/@some.name_1-2", "@some.name_1-2"},
		{"https://www.youtube.com/channel/UCSJ4gkVC6NrvII8umztf0Ow", "channel/UCSJ4gkVC6NrvII8umztf0Ow"},
		{"UCSJ4gkVC6NrvII8umztf0Ow", "channel/UCSJ4gkVC6NrvII8umztf0Ow"},
		{"https://youtube.com/c/SomeName", "c/SomeName"},
		{"https://www.youtube.com/user/oldname/", "user/oldname"},
	};
	for (const auto &c : cases) {
		YouTubeTarget t = ParseYouTubeTarget(c.in);
		CHECK_EQ(t.kind, Kind::Channel);
		CHECK_EQ(t.channel_path, std::string(c.path));
	}
	CHECK_EQ(YouTubeLivePageUrl(ParseYouTubeTarget("@LofiGirl")), std::string("https://www.youtube.com/@LofiGirl/live"));
}

TEST_CASE(youtube_web_target_videos_and_invalid)
{
	YouTubeTarget v = ParseYouTubeTarget("https://www.youtube.com/watch?v=abcDEF12345&t=3");
	CHECK_EQ(v.kind, Kind::Video);
	CHECK_EQ(v.video_id, std::string("abcDEF12345"));
	CHECK_EQ(ParseYouTubeTarget("youtu.be/abcDEF12345").video_id, std::string("abcDEF12345"));
	CHECK_EQ(ParseYouTubeTarget("https://www.youtube.com/live/abcDEF12345").kind, Kind::Video);

	// A bare 11-character word is tried as a handle first, then as a video.
	YouTubeTarget bare = ParseYouTubeTarget("abcDEF12345");
	CHECK_EQ(bare.kind, Kind::Channel);
	CHECK(bare.could_be_video);
	CHECK_EQ(bare.video_id, std::string("abcDEF12345"));
	CHECK(!ParseYouTubeTarget("shortname").could_be_video);

	const char *bad[] = {"", "   ", "https://example.invalid/watch?v=abcDEF12345", "@", "@bad name",
			     "youtube.com/channel/notachannel", "two words", "@x<y>"};
	for (const char *b : bad)
		CHECK_EQ(ParseYouTubeTarget(b).kind, Kind::Invalid);
}

TEST_CASE(youtube_web_canonical)
{
	CHECK(!FindCanonicalVideoId("<html><head><title>x</title>").has_value());
	CHECK(!FindCanonicalVideoId("<link rel=\"canonical\" href=\"https://www.youtu").has_value()); // cut off
	auto live = FindCanonicalVideoId("...<link rel=\"canonical\" href=\"https://www.youtube.com/watch?v=0sfDth4lMLc\">");
	CHECK(live.has_value());
	CHECK_EQ(*live, std::string("0sfDth4lMLc"));
	auto offline =
		FindCanonicalVideoId("<link rel=\"canonical\" href=\"https://www.youtube.com/channel/UCX6OQ3DkcsbYNE6H8uQQuVA\">");
	CHECK(offline.has_value());
	CHECK(offline->empty());
}

namespace {

std::string ChatPage(const std::string &contents)
{
	return "<html><script>ytcfg.set({\"INNERTUBE_CLIENT_VERSION\":\"2.20261020.05.00\",\"OTHER\":1});</script>"
	       "<script>window[\"ytInitialData\"] = " +
	       contents + ";</script></html>";
}

} // namespace

TEST_CASE(youtube_web_chat_page)
{
	std::string page = ChatPage(R"({"contents":{"liveChatRenderer":{
		"continuations":[{"invalidationContinuationData":{"timeoutMs":10000,"continuation":"INVAL"}}],
		"header":{"liveChatHeaderRenderer":{"viewSelector":{"sortFilterSubMenuRenderer":{"subMenuItems":[
			{"title":"Top chat","selected":true,"continuation":{"reloadContinuationData":{"continuation":"TOP"}}},
			{"title":"Live chat","selected":false,"continuation":{"reloadContinuationData":{"continuation":"ALL{}\"x"}}}
		]}}}}}}})");
	YouTubeWebChatInfo info = ParseYouTubeLiveChatHtml(page);
	CHECK(info.parsed);
	CHECK(info.has_chat);
	CHECK_EQ(info.client_version, std::string("2.20261020.05.00"));
	CHECK_EQ(info.continuation, std::string("ALL{}\"x")); // the full "Live chat" view, not "Top chat"

	YouTubeWebChatInfo off =
		ParseYouTubeLiveChatHtml(ChatPage(R"({"contents":{"messageRenderer":{"text":{"runs":[{"text":"Chat is disabled for this live stream."}]}}}})"));
	CHECK(off.parsed);
	CHECK(!off.has_chat);
	CHECK_EQ(off.message, std::string("Chat is disabled for this live stream."));

	CHECK(!ParseYouTubeLiveChatHtml("<html>unsupported browser</html>").parsed);

	std::string body = BuildYouTubeGetLiveChatBody("2.1", "C\"1");
	CHECK(body.find("\"clientName\":\"WEB\"") != std::string::npos);
	CHECK(body.find("\"continuation\":\"C\\\"1\"") != std::string::npos);
}

TEST_CASE(youtube_web_get_live_chat)
{
	const std::string body = R"json({"responseContext":{},"continuationContents":{"liveChatContinuation":{
		"continuations":[{"invalidationContinuationData":{"continuation":"NEXT","timeoutMs":10000}}],
		"actions":[
		 {"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{
			"id":"m1","timestampUsec":"1792628985209089","authorExternalChannelId":"UCaaa",
			"authorName":{"simpleText":"@viewer"},
			"authorBadges":[{"liveChatAuthorBadgeRenderer":{"icon":{"iconType":"MODERATOR"}}},
			                {"liveChatAuthorBadgeRenderer":{"customThumbnail":{"thumbnails":[]},"tooltip":"Member (2 months)"}}],
			"message":{"runs":[{"text":"hi "},
			  {"emoji":{"emojiId":"😘","shortcuts":[":kissing_heart:"]}},
			  {"text":" "},
			  {"emoji":{"emojiId":"UCx/abc","shortcuts":[":wave:"],"isCustomEmoji":true,
			    "image":{"thumbnails":[{"url":"https://yt3.ggpht.com/a=w24"},{"url":"https://yt3.ggpht.com/a=w48"}]}}}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatPaidMessageRenderer":{
			"id":"m2","timestampUsec":"1","authorName":{"simpleText":"@fan"},
			"purchaseAmountText":{"simpleText":"$5.00"},"message":{"runs":[{"text":"great stream"}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatMembershipItemRenderer":{
			"id":"m3","authorName":{"simpleText":"@new"},"headerSubtext":{"runs":[{"text":"Welcome to "},{"text":"Channel"},{"text":"!"}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatViewerEngagementMessageRenderer":{"id":"e1"}}}},
		 {"addChatItemAction":{"item":{"liveChatPlaceholderItemRenderer":{"id":"p1"}}}},
		 {"removeChatItemAction":{"targetItemId":"m0"}},
		 {"removeChatItemByAuthorAction":{"externalChannelId":"UCbad"}},
		 {"markChatItemAsDeletedAction":{"targetItemId":"m9"}}
		]}}})json";
	YouTubeWebChatBatch b = ParseYouTubeGetLiveChat(body, 42);
	CHECK(b.ok);
	CHECK(!b.ended);
	CHECK_EQ(b.items.size(), (size_t)6);

	const ChatMessage &m = b.items[0].message;
	CHECK_EQ(m.id, std::string("m1"));
	CHECK_EQ(m.author, std::string("@viewer"));
	CHECK_EQ(m.author_id, std::string("UCaaa"));
	CHECK_EQ(m.timestamp_ms, (int64_t)1792628985209);
	CHECK_EQ(m.text, std::string("hi \xF0\x9F\x98\x98 :wave:"));
	CHECK_EQ(m.badges.size(), (size_t)2);
	CHECK_EQ(m.badges[0], std::string("moderator"));
	CHECK_EQ(m.badges[1], std::string("member"));
	CHECK_EQ(m.emotes.size(), (size_t)1);
	CHECK_EQ(m.text.substr(m.emotes[0].begin, m.emotes[0].end - m.emotes[0].begin), std::string(":wave:"));
	CHECK_EQ(m.emotes[0].url, std::string("https://yt3.ggpht.com/a=w48"));
	CHECK_EQ(m.emotes[0].source, std::string("youtube"));

	CHECK_EQ(b.items[1].message.text, std::string("[Super Chat $5.00] great stream"));
	CHECK_EQ(b.items[1].message.timestamp_ms, (int64_t)42); // a timestamp too small to be real falls back to "now"
	CHECK_EQ(b.items[2].message.text, std::string("[Welcome to Channel!]"));
	CHECK_EQ(b.items[3].kind, YouTubeChatItem::Kind::Delete);
	CHECK_EQ(b.items[3].target_id, std::string("m0"));
	CHECK_EQ(b.items[4].kind, YouTubeChatItem::Kind::ClearUser);
	CHECK_EQ(b.items[4].target_id, std::string("UCbad"));
	CHECK_EQ(b.items[5].target_id, std::string("m9"));
}

TEST_CASE(youtube_web_get_live_chat_end_and_garbage)
{
	YouTubeWebChatBatch ended =
		ParseYouTubeGetLiveChat(R"({"responseContext":{},"continuationContents":{"liveChatContinuation":{"actions":[]}}})", 0);
	CHECK(ended.ok);
	CHECK(ended.ended);
	YouTubeWebChatBatch gone = ParseYouTubeGetLiveChat(R"({"responseContext":{"x":1}})", 0);
	CHECK(gone.ok);
	CHECK(gone.ended);
	CHECK(!ParseYouTubeGetLiveChat("not json", 0).ok);
	CHECK(!ParseYouTubeGetLiveChat("[]", 0).ok);
	CHECK(!ParseYouTubeGetLiveChat(R"({"error":{"code":400}})", 0).ok);
}
