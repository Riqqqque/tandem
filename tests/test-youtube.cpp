// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "parse-youtube.h"

#include <random>

using namespace tandem::chat;
using Kind = YouTubeChatItem::Kind;
using C = YouTubeErrorClass;

namespace {
constexpr int64_t kNow = 99;
}

TEST_CASE(youtube_video_id_extraction)
{
	const std::string id = "abcDEF12345";
	const char *ok[] = {
		"abcDEF12345",
		"  abcDEF12345 ",
		"https://www.youtube.com/watch?v=abcDEF12345",
		"https://youtube.com/watch?v=abcDEF12345&t=42s",
		"https://www.youtube.com/watch?feature=share&v=abcDEF12345&list=PL123",
		"http://m.youtube.com/watch?v=abcDEF12345",
		"youtube.com/watch?v=abcDEF12345",
		"www.youtube.com/watch?v=abcDEF12345#comments",
		"https://youtu.be/abcDEF12345",
		"https://youtu.be/abcDEF12345?si=XyZ123&t=5",
		"youtu.be/abcDEF12345",
		"https://www.youtube.com/live/abcDEF12345",
		"https://www.youtube.com/live/abcDEF12345?feature=shared",
		"https://m.youtube.com/live/abcDEF12345?si=abc",
		"https://www.youtube.com/shorts/abcDEF12345",
		"https://youtube.com/shorts/abcDEF12345?feature=share",
		"https://www.youtube.com/embed/abcDEF12345?autoplay=1",
		"https://music.youtube.com/watch?v=abcDEF12345",
		"HTTPS://WWW.YOUTUBE.COM/watch?v=abcDEF12345",
		"https://www.youtube.com:443/watch?v=abcDEF12345",
	};
	for (const char *s : ok) {
		auto got = ExtractYouTubeVideoId(s);
		if (!got || *got != id)
			tt::Fail(__FILE__, __LINE__, std::string("expected id from ") + s);
	}
	const char *bad[] = {
		"",
		"abcDEF1234",
		"abcDEF123456",
		"abc DEF12345",
		"https://www.youtube.com/watch?v=short",
		"https://www.youtube.com/watch?x=abcDEF12345",
		"https://www.youtube.com/@SomeChannel/live",
		"https://www.youtube.com/channel/UCaaaaaaaaaaaaaaaaaaaaaa",
		"https://evil.example/watch?v=abcDEF12345",
		"https://youtube.com.evil.example/watch?v=abcDEF12345",
		"https://notyoutube.com/watch?v=abcDEF12345",
		"https://youtu.be/",
	};
	for (const char *s : bad) {
		if (ExtractYouTubeVideoId(s))
			tt::Fail(__FILE__, __LINE__, std::string("expected no id from ") + s);
	}
}

TEST_CASE(youtube_channel_id_and_urlencode)
{
	CHECK(IsValidYouTubeChannelId("UCaaaaaaaaaaaaaaaaaaaaaa"));
	CHECK(IsValidYouTubeChannelId("UC_x5XG1OV2P6uZZ5FSM9Ttw"));
	CHECK(!IsValidYouTubeChannelId("UCaaaaaaaaaaaaaaaaaaaaa"));
	CHECK(!IsValidYouTubeChannelId("XXaaaaaaaaaaaaaaaaaaaaaa"));
	CHECK(!IsValidYouTubeChannelId("UCaaaaaaaaaaaaaaaaaaaa a"));
	CHECK_EQ(UrlEncode("Ab-_.~ +/=&?"), std::string("Ab-_.~%20%2B%2F%3D%26%3F"));
}

namespace {
std::vector<std::string> SplitAll(const std::string &data, const std::vector<size_t> &cuts)
{
	JsonObjectStreamSplitter s;
	std::vector<std::string> out;
	size_t p = 0;
	for (size_t c : cuts) {
		s.Feed(std::string_view(data).substr(p, c - p), [&](std::string &&o) { out.push_back(std::move(o)); });
		p = c;
	}
	s.Feed(std::string_view(data).substr(p), [&](std::string &&o) { out.push_back(std::move(o)); });
	return out;
}
} // namespace

TEST_CASE(youtube_stream_splitter_whole_and_random_chunks)
{
	const std::string data = tt::ReadFixture("youtube-stream.json");
	auto whole = SplitAll(data, {});
	CHECK_EQ(whole.size(), size_t(3));
	if (whole.size() != 3)
		return;
	CHECK(whole[0].front() == '{' && whole[0].back() == '}');

	// Byte-by-byte.
	std::vector<size_t> every;
	for (size_t i = 1; i < data.size(); ++i)
		every.push_back(i);
	CHECK(SplitAll(data, every) == whole);

	// Random chunk sizes, many seeds.
	for (unsigned seed = 1; seed <= 300; ++seed) {
		std::mt19937 gen(seed);
		std::uniform_int_distribution<size_t> step(1, seed % 3 == 0 ? 7 : 400);
		std::vector<size_t> cuts;
		for (size_t p = step(gen); p < data.size(); p += step(gen))
			cuts.push_back(p);
		if (SplitAll(data, cuts) != whole) {
			tt::Fail(__FILE__, __LINE__, "random split mismatch for seed " + std::to_string(seed));
			break;
		}
	}

	// Cut exactly inside the escaped quote / brace sequences of the tricky message.
	const std::string needle = R"(\\\" tricky \\\\ end }})";
	size_t at = data.find(needle);
	CHECK(at != std::string::npos);
	for (size_t k = 0; k <= needle.size(); ++k)
		CHECK(SplitAll(data, {at + k}) == whole);
}

TEST_CASE(youtube_stream_splitter_edge_cases)
{
	std::vector<std::string> out;
	JsonObjectStreamSplitter s;
	auto sink = [&](std::string &&o) { out.push_back(std::move(o)); };
	s.Feed("[", sink);
	s.Feed("]", sink);
	CHECK(out.empty());
	s.Feed("[{\"a\":\"}\"},{\"b\":[1,{\"c\":\"\\\\\"}]}]", sink);
	CHECK_EQ(out.size(), size_t(2));
	if (out.size() == 2) {
		CHECK_EQ(out[0], std::string("{\"a\":\"}\"}"));
		CHECK_EQ(out[1], std::string("{\"b\":[1,{\"c\":\"\\\\\"}]}"));
	}
	// An unterminated object stays pending.
	out.clear();
	s.Reset();
	s.Feed("[{\"a\":1", sink);
	CHECK(out.empty());
	CHECK(s.mid_object());
	s.Feed("}", sink);
	CHECK_EQ(out.size(), size_t(1));

	// Oversized objects are dropped and the splitter resyncs on the next one.
	JsonObjectStreamSplitter small(16);
	out.clear();
	small.Feed("[{\"big\":\"0123456789012345678901234567890\"},{\"ok\":1}]", sink);
	CHECK_EQ(out.size(), size_t(1));
	if (!out.empty())
		CHECK_EQ(out[0], std::string("{\"ok\":1}"));
}

TEST_CASE(youtube_stream_pages)
{
	auto objs = SplitAll(tt::ReadFixture("youtube-stream.json"), {});
	if (objs.size() != 3) {
		tt::Fail(__FILE__, __LINE__, "fixture did not split into 3 objects");
		return;
	}
	auto p1 = ParseYouTubeChatPage(objs[0], kNow);
	CHECK(p1.ok);
	CHECK_EQ(p1.next_page_token, std::string("TOKEN_PAGE_1"));
	CHECK_EQ(p1.items.size(), size_t(2));
	if (p1.items.size() == 2) {
		const auto &m = p1.items[0].message;
		CHECK_EQ(p1.items[0].kind, Kind::Message);
		CHECK_EQ(m.id, std::string("LCC.test-message-0001"));
		CHECK_EQ(m.author, std::string("Sample Viewer"));
		CHECK_EQ(m.author_id, std::string("UCaaaaaaaaaaaaaaaaaaaaaa"));
		CHECK_EQ(m.text, std::string("brace test {\"not\": [an object]} and \\\" tricky \\\\ end }}"));
		CHECK_EQ(m.timestamp_ms, 1791397815123LL);
		CHECK(m.badges == std::vector<std::string>({"moderator", "member"}));
		CHECK(m.platform == Platform::YouTube);
		const auto &sc = p1.items[1].message;
		CHECK_EQ(sc.text, std::string("[Super Chat $5.00] Great stream \xc3\xa9"));
		CHECK(sc.badges == std::vector<std::string>({"verified"}));
	}

	auto p2 = ParseYouTubeChatPage(objs[1], kNow);
	CHECK(p2.ok);
	CHECK(!p2.offline);
	CHECK_EQ(p2.items.size(), size_t(5)); // pollEvent skipped
	if (p2.items.size() == 5) {
		CHECK_EQ(p2.items[0].kind, Kind::Delete);
		CHECK_EQ(p2.items[0].target_id, std::string("LCC.test-message-0001"));
		CHECK_EQ(p2.items[1].kind, Kind::ClearUser);
		CHECK_EQ(p2.items[1].target_id, std::string("UCbbbbbbbbbbbbbbbbbbbbbb"));
		CHECK_EQ(p2.items[2].message.text, std::string("[New member: Gold]"));
		CHECK_EQ(p2.items[3].message.text, std::string("[Member for 12 months] a year! [ok]"));
		CHECK_EQ(p2.items[4].message.text, std::string("[Super Sticker \xe2\x82\xac" "2,00] Dancing cat"));
		CHECK(p2.items[4].message.badges.empty());
	}

	auto p3 = ParseYouTubeChatPage(objs[2], kNow);
	CHECK(p3.ok);
	CHECK(p3.offline);
	CHECK_EQ(p3.items.size(), size_t(1));
	if (!p3.items.empty())
		CHECK_EQ(p3.items[0].kind, Kind::ChatEnded);
}

TEST_CASE(youtube_poll_page)
{
	auto p = ParseYouTubeChatPage(tt::ReadFixture("youtube-poll-page.json"), kNow);
	CHECK(p.ok);
	CHECK_EQ(p.polling_interval_ms, int64_t(5172));
	CHECK_EQ(p.next_page_token, std::string("GPOLLTOKEN"));
	CHECK_EQ(p.items.size(), size_t(1));
	if (!p.items.empty()) {
		CHECK_EQ(p.items[0].message.text, std::string("polled hello"));
		CHECK_EQ(p.items[0].message.timestamp_ms, 1791398400500LL);
		CHECK(p.items[0].message.badges == std::vector<std::string>({"owner"}));
	}
	CHECK(!ParseYouTubeChatPage("not json", kNow).ok);
	CHECK(!ParseYouTubeChatPage("[]", kNow).ok);
	auto weird = ParseYouTubeChatPage(R"({"items":[5,"x",null,{"snippet":[]},{"snippet":{"type":"textMessageEvent"}}]})", kNow);
	CHECK(weird.ok);
	CHECK_EQ(weird.items.size(), size_t(1));
	if (!weird.items.empty())
		CHECK_EQ(weird.items[0].message.timestamp_ms, kNow);
	auto err = ParseYouTubeChatPage(tt::ReadFixture("youtube-error-chat-ended.json"), kNow);
	CHECK(err.has_error);
	CHECK_EQ(ClassifyYouTubeError(err.error), C::ChatEnded);
}

TEST_CASE(youtube_errors)
{
	auto quota = ParseYouTubeError(403, tt::ReadFixture("youtube-error-quota.json"));
	CHECK_EQ(ClassifyYouTubeError(quota), C::QuotaExhausted);
	CHECK(IsFatalYouTubeError(C::QuotaExhausted));
	CHECK_EQ(YouTubeErrorText(C::QuotaExhausted, quota), std::string("YouTube API quota exhausted for today"));

	auto ended = ParseYouTubeError(403, tt::ReadFixture("youtube-error-chat-ended.json"));
	CHECK_EQ(ClassifyYouTubeError(ended), C::ChatEnded);
	CHECK_EQ(YouTubeErrorText(C::ChatEnded, ended), std::string("Live chat has ended"));

	auto key = ParseYouTubeError(400, tt::ReadFixture("youtube-error-key-invalid.json"));
	CHECK_EQ(ClassifyYouTubeError(key), C::InvalidKey);
	CHECK(IsFatalYouTubeError(C::InvalidKey));

	auto forbidden = ParseYouTubeError(403, tt::ReadFixture("youtube-error-forbidden.json"));
	CHECK_EQ(ClassifyYouTubeError(forbidden), C::Forbidden);

	auto html404 = ParseYouTubeError(404, tt::ReadFixture("youtube-error-not-found.html"));
	CHECK_EQ(ClassifyYouTubeError(html404), C::NotFound);
	CHECK(!IsFatalYouTubeError(C::NotFound));

	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(503, "")), C::Transient);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(0, "")), C::Transient);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(429, "{}")), C::RateLimited);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(501, "")), C::Transient);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(400, R"({"error":{"code":400,"message":"bad","errors":[{"reason":"invalidPageToken"}]}})")),
		 C::Other);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(403, R"({"error":{"errors":[{"reason":"liveChatDisabled"}]}})")),
		 C::ChatDisabled);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(403, R"({"error":{"errors":[{"reason":"accessNotConfigured"}]}})")),
		 C::ApiNotEnabled);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(403, R"({"error":{"details":[{"reason":"API_KEY_SERVICE_BLOCKED"}]}})")),
		 C::KeyRestricted);
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(403, R"({"error":{"errors":[{"reason":"rateLimitExceeded"}]}})")),
		 C::RateLimited);
	// Streamed error bodies may be wrapped in an array.
	CHECK_EQ(ClassifyYouTubeError(ParseYouTubeError(403, "[" + tt::ReadFixture("youtube-error-quota.json") + "]")),
		 C::QuotaExhausted);
	// Messages never echo anything beyond the server's text.
	auto other = ParseYouTubeError(400, R"({"error":{"code":400,"message":"Invalid value"}})");
	CHECK_EQ(YouTubeErrorText(ClassifyYouTubeError(other), other), std::string("YouTube API error (HTTP 400): Invalid value"));
}

TEST_CASE(youtube_videos_and_search)
{
	auto live = ParseYouTubeVideosList(tt::ReadFixture("youtube-videos-live.json"));
	CHECK(live.has_value());
	if (live) {
		CHECK(live->found);
		CHECK(live->is_live_broadcast);
		CHECK(!live->ended);
		CHECK_EQ(live->active_chat_id, std::string("TEST_LIVE_CHAT_ID_+/="));
	}
	auto upcoming = ParseYouTubeVideosList(tt::ReadFixture("youtube-videos-upcoming.json"));
	CHECK(upcoming && upcoming->found && upcoming->is_live_broadcast && upcoming->active_chat_id.empty());
	auto missing = ParseYouTubeVideosList(R"({"items":[]})");
	CHECK(missing && !missing->found);
	auto vod = ParseYouTubeVideosList(R"({"items":[{"id":"abcDEF12345"}]})");
	CHECK(vod && vod->found && !vod->is_live_broadcast);
	auto ended = ParseYouTubeVideosList(
		R"({"items":[{"liveStreamingDetails":{"actualStartTime":"2026-10-07T17:00:00Z","actualEndTime":"2026-10-07T19:00:00Z"}}]})");
	CHECK(ended && ended->ended && ended->active_chat_id.empty());
	CHECK(!ParseYouTubeVideosList("<html>"));

	CHECK_EQ(ParseYouTubeSearchLive(R"({"items":[{"id":{"kind":"youtube#video","videoId":"abcDEF12345"}}]})").value_or("?"),
		 std::string("abcDEF12345"));
	CHECK_EQ(ParseYouTubeSearchLive(R"({"items":[]})").value_or("?"), std::string());
	CHECK(!ParseYouTubeSearchLive("oops"));
}
