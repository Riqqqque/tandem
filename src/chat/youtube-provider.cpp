// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - read-only YouTube live chat through the YouTube Data API v3 and the user's own key.
// The key is sent only in the X-Goog-Api-Key header and is never logged or put in a URL.
#include "backoff.h"
#include "chat-util.h"
#include "net.h"
#include "parse-youtube.h"
#include "provider-base.h"

#include <optional>

namespace tandem::chat {

namespace {

constexpr const char *kVideosUrl = "https://www.googleapis.com/youtube/v3/videos?part=liveStreamingDetails,snippet&id=";
constexpr const char *kSearchUrl = "https://www.googleapis.com/youtube/v3/search?part=id&eventType=live&type=video"
				   "&maxResults=1&channelId=";
constexpr const char *kStreamUrl = "https://youtube.googleapis.com/youtube/v3/liveChat/messages/stream";
constexpr const char *kPollUrl = "https://www.googleapis.com/youtube/v3/liveChat/messages";
constexpr const char *kChatParams = "&part=snippet,authorDetails&maxResults=200";

// Estimated quota costs (units). These are deliberately conservative:
// - videos.list costs 1 unit.
// - search.list costs 100 units and, as of 2026, also counts against a separate daily bucket
//   of 100 search calls per project.
// - liveChatMessages.list is documented at 1 unit today but historically cost 5; keep 5.
// - streamList is billed per call; count 5 units for every (re)connection.
constexpr int64_t kCostVideosList = 1;
constexpr int64_t kCostSearch = 100;
constexpr int64_t kCostPoll = 5;
constexpr int64_t kCostStreamConnect = 5;

constexpr auto kWaitForLiveVideo = std::chrono::seconds(60);
// A channel search costs 100 units, so a channel waiting to go live is checked less often.
constexpr auto kWaitForLiveChannel = std::chrono::minutes(5);
constexpr long kStreamIdleTimeoutMs = 120000;
constexpr auto kMinHealthyStream = std::chrono::seconds(10);

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

class YouTubeProvider final : public ProviderBase {
public:
	YouTubeProvider(YouTubeChatConfig cfg, ChatSink *sink)
		: ProviderBase(Platform::YouTube, sink),
		  cfg_(std::move(cfg)),
		  quota_(cfg_.quota_used_today)
	{
	}
	~YouTubeProvider() override { Stop(); }

protected:
	void Run() override;

private:
	enum class End { Stop, Fatal, Retry, Fallback, Resume };
	struct Outcome {
		End end = End::Retry;
		std::string reason;
		long retry_after_s = 0;
	};

	HttpResponse Get(const std::string &url);
	void AddQuota(int64_t units);
	std::optional<std::string> ResolveChatId();
	// Handles a non-success API response; returns Fatal or Retry.
	Outcome ApiFailure(const HttpResponse &r);
	Outcome RunStream();
	Outcome RunPoll();
	// Posts the page's items. Returns true when the chat has ended.
	bool HandlePage(const YouTubeChatPage &page);
	void MarkConnected(const char *transport);

	YouTubeChatConfig cfg_;
	std::string key_;
	int64_t quota_;
	std::string chat_id_; // cached for the provider lifetime
	std::string page_token_;
	bool use_stream_ = true;
	bool logged_transport_ = false;
	bool connected_ = false;
	RecentIds seen_{2000};
	Backoff backoff_;
};

HttpResponse YouTubeProvider::Get(const std::string &url)
{
	// No redirects: libcurl would re-send the key header to whatever host a redirect names.
	return HttpGet(url, {"X-Goog-Api-Key: " + key_, "Accept: application/json"}, 15000, stop_flag(), false);
}

void YouTubeProvider::AddQuota(int64_t units)
{
	quota_ += units;
	PostQuota(quota_);
	if (cfg_.on_quota) {
		try {
			cfg_.on_quota(quota_);
		} catch (...) {
		}
	}
}

YouTubeProvider::Outcome YouTubeProvider::ApiFailure(const HttpResponse &r)
{
	Outcome o;
	o.retry_after_s = r.retry_after_s;
	if (r.status == 0) {
		o.end = End::Retry;
		o.reason = "YouTube API unreachable (" + r.error + ")";
		return o;
	}
	YouTubeApiError e = ParseYouTubeError(r.status, r.body);
	YouTubeErrorClass c = ClassifyYouTubeError(e);
	o.reason = YouTubeErrorText(c, e);
	o.end = IsFatalYouTubeError(c) || c == YouTubeErrorClass::Other || c == YouTubeErrorClass::NotFound ? End::Fatal
														: End::Retry;
	return o;
}

void YouTubeProvider::Run()
{
	key_ = std::string(TrimAscii(cfg_.api_key));
	if (key_.empty()) {
		PostStatus(ProviderState::Error, "YouTube API key is missing");
		return;
	}
	backoff_.Reset();
	connected_ = false;

	if (chat_id_.empty()) {
		auto id = ResolveChatId();
		if (!id)
			return;
		chat_id_ = *id;
	}

	while (!stopping()) {
		PostStatus(ProviderState::Connecting, "live chat " + chat_id_);
		auto started = Clock::now();
		Outcome o = use_stream_ ? RunStream() : RunPoll();
		if (o.end == End::Stop || stopping())
			return;
		if (o.end == End::Fatal) {
			PostStatus(ProviderState::Error, o.reason);
			return;
		}
		if (o.end == End::Fallback) {
			use_stream_ = false;
			Log(LogLevel::Info, "[chat/youtube] streamList unavailable (%s); falling back to polling",
			    o.reason.c_str());
			continue;
		}
		milliseconds delay;
		auto now = Clock::now();
		if (o.end == End::Resume && now - started >= kMinHealthyStream) {
			delay = milliseconds(500);
		} else {
			delay = backoff_.Next(now, std::chrono::seconds(o.retry_after_s));
		}
		connected_ = false;
		PostStatus(ProviderState::Reconnecting, o.reason + ", " + RetryText(delay));
		if (!WaitFor(delay))
			return;
	}
}

std::optional<std::string> YouTubeProvider::ResolveChatId()
{
	std::string video_id;
	const bool channel_mode = TrimAscii(cfg_.video).empty();
	std::string channel_id(TrimAscii(cfg_.channel_id));
	if (!channel_mode) {
		auto id = ExtractYouTubeVideoId(cfg_.video);
		if (!id) {
			PostStatus(ProviderState::Error, "Not a YouTube video ID or URL");
			return std::nullopt;
		}
		video_id = *id;
	} else if (channel_id.empty()) {
		PostStatus(ProviderState::Error, "Set a YouTube video or channel ID");
		return std::nullopt;
	} else if (!IsValidYouTubeChannelId(channel_id)) {
		PostStatus(ProviderState::Error, "Invalid YouTube channel ID (expected UC...)");
		return std::nullopt;
	}

	auto fail = [&](const HttpResponse &r) -> bool {
		// Returns true when the caller should retry.
		Outcome o = ApiFailure(r);
		if (o.end == End::Fatal) {
			PostStatus(ProviderState::Error, o.reason);
			return false;
		}
		auto delay = backoff_.Next(Clock::now(), std::chrono::seconds(o.retry_after_s));
		PostStatus(ProviderState::Reconnecting, o.reason + ", " + RetryText(delay));
		return WaitFor(delay);
	};
	auto wait_live = [&](const char *what, milliseconds d) {
		PostStatus(ProviderState::Connecting, what);
		return WaitFor(d);
	};

	while (!stopping()) {
		PostStatus(ProviderState::Connecting, "Looking up the live chat");
		std::string vid = video_id;
		if (channel_mode) {
			HttpResponse r = Get(kSearchUrl + UrlEncode(channel_id));
			if (stopping())
				return std::nullopt;
			if (r.status != 0)
				AddQuota(kCostSearch);
			if (!r.error.empty() || r.status != 200) {
				if (!fail(r))
					return std::nullopt;
				continue;
			}
			auto found = ParseYouTubeSearchLive(r.body);
			if (!found || found->empty()) {
				if (!wait_live("Waiting for the stream to go live", kWaitForLiveChannel))
					return std::nullopt;
				continue;
			}
			vid = *found;
		}

		HttpResponse r = Get(kVideosUrl + vid);
		if (stopping())
			return std::nullopt;
		if (r.status != 0)
			AddQuota(kCostVideosList);
		if (!r.error.empty() || r.status != 200) {
			if (!fail(r))
				return std::nullopt;
			continue;
		}
		auto info = ParseYouTubeVideosList(r.body);
		if (!info) {
			auto delay = backoff_.Next(Clock::now());
			PostStatus(ProviderState::Reconnecting, "Unexpected response from YouTube, " + RetryText(delay));
			if (!WaitFor(delay))
				return std::nullopt;
			continue;
		}
		if (!info->active_chat_id.empty()) {
			backoff_.Reset();
			PostChannelInfo(info->channel_id);
			Log(LogLevel::Info, "[chat/youtube] video %s -> live chat %s", vid.c_str(),
			    info->active_chat_id.c_str());
			return info->active_chat_id;
		}
		if (!channel_mode) {
			if (!info->found) {
				PostStatus(ProviderState::Error, "YouTube video not found");
				return std::nullopt;
			}
			if (!info->is_live_broadcast) {
				PostStatus(ProviderState::Error, "This video is not a live stream");
				return std::nullopt;
			}
			if (info->ended) {
				PostStatus(ProviderState::Error, "This live stream has ended");
				return std::nullopt;
			}
		}
		if (!wait_live("Waiting for the stream to go live",
			       channel_mode ? milliseconds(kWaitForLiveChannel) : milliseconds(kWaitForLiveVideo)))
			return std::nullopt;
	}
	return std::nullopt;
}

void YouTubeProvider::MarkConnected(const char *transport)
{
	if (connected_)
		return;
	connected_ = true;
	backoff_.MarkConnected(Clock::now());
	if (!logged_transport_) {
		logged_transport_ = true;
		Log(LogLevel::Info, "[chat/youtube] using %s transport", transport);
	}
	PostStatus(ProviderState::Connected, "live chat " + chat_id_);
}

bool YouTubeProvider::HandlePage(const YouTubeChatPage &page)
{
	bool ended = page.offline;
	for (const auto &it : page.items) {
		switch (it.kind) {
		case YouTubeChatItem::Kind::Message:
			if (seen_.Insert(it.message.id)) {
				ChatMessage m = it.message;
				PostChatMessage(std::move(m));
			}
			break;
		case YouTubeChatItem::Kind::Delete:
			PostDelete(it.target_id);
			break;
		case YouTubeChatItem::Kind::ClearUser:
			PostClearUser(it.target_id);
			break;
		case YouTubeChatItem::Kind::ChatEnded:
			ended = true;
			break;
		}
	}
	if (!page.next_page_token.empty())
		page_token_ = page.next_page_token;
	return ended;
}

YouTubeProvider::Outcome YouTubeProvider::RunStream()
{
	std::string url = std::string(kStreamUrl) + "?liveChatId=" + UrlEncode(chat_id_) + kChatParams;
	if (!page_token_.empty())
		url += "&pageToken=" + UrlEncode(page_token_);

	JsonObjectStreamSplitter splitter;
	bool ended = false;
	bool got_page = false;
	std::optional<YouTubeApiError> inline_error;
	auto on_object = [&](std::string &&obj) {
		YouTubeChatPage page = ParseYouTubeChatPage(obj, NowMs());
		if (page.has_error) {
			inline_error = page.error;
			return;
		}
		if (!page.ok)
			return;
		got_page = true;
		MarkConnected("streamList");
		if (HandlePage(page))
			ended = true;
	};
	auto on_chunk = [&](std::string_view chunk) {
		splitter.Feed(chunk, on_object);
		return !stopping() && !ended && !inline_error;
	};

	HttpResponse r = HttpStream(url, {"X-Goog-Api-Key: " + key_, "Accept: application/json"}, on_chunk,
				    stop_flag(), kStreamIdleTimeoutMs, false);
	if (r.status != 0)
		AddQuota(kCostStreamConnect);
	if (stopping())
		return {End::Stop};
	if (ended)
		return {End::Fatal, "Live chat has ended"};

	Outcome o;
	o.retry_after_s = r.retry_after_s;
	if (inline_error || r.status >= 300) {
		YouTubeApiError e = inline_error ? *inline_error : ParseYouTubeError(r.status, r.body);
		if (e.http_status == 0)
			e.http_status = r.status;
		YouTubeErrorClass c = ClassifyYouTubeError(e);
		o.reason = YouTubeErrorText(c, e);
		if (IsFatalYouTubeError(c)) {
			o.end = End::Fatal;
		} else if (c == YouTubeErrorClass::RateLimited || c == YouTubeErrorClass::Transient) {
			o.end = End::Retry;
		} else {
			// 400/404/501 or a 403 that is not about quota, the key or this chat.
			o.end = End::Fallback;
			o.reason = "HTTP " + std::to_string(e.http_status);
		}
		return o;
	}
	if (!r.error.empty()) {
		o.end = got_page && r.idle_timeout ? End::Resume : End::Retry;
		o.reason = r.idle_timeout ? "Chat stream went quiet" : "Chat stream interrupted (" + r.error + ")";
		return o;
	}
	o.end = got_page ? End::Resume : End::Retry;
	o.reason = "Chat stream closed by YouTube";
	return o;
}

YouTubeProvider::Outcome YouTubeProvider::RunPoll()
{
	for (;;) {
		std::string url = std::string(kPollUrl) + "?liveChatId=" + UrlEncode(chat_id_) + kChatParams;
		if (!page_token_.empty())
			url += "&pageToken=" + UrlEncode(page_token_);
		HttpResponse r = Get(url);
		if (stopping())
			return {End::Stop};
		if (r.status != 0)
			AddQuota(kCostPoll);
		if (!r.error.empty() || r.status != 200)
			return ApiFailure(r);
		YouTubeChatPage page = ParseYouTubeChatPage(r.body, NowMs());
		if (page.has_error) {
			HttpResponse fake = r;
			fake.status = page.error.http_status ? page.error.http_status : 400;
			return ApiFailure(fake);
		}
		if (!page.ok)
			return {End::Retry, "Unexpected response from YouTube"};
		MarkConnected("polling");
		if (HandlePage(page))
			return {End::Fatal, "Live chat has ended"};
		auto interval = std::max<int64_t>(1000, page.polling_interval_ms > 0 ? page.polling_interval_ms : 5000);
		if (!WaitFor(milliseconds(interval)))
			return {End::Stop};
	}
}

} // namespace

std::unique_ptr<ChatProvider> CreateYouTubeProvider(YouTubeChatConfig cfg, ChatSink *sink)
{
	return std::make_unique<YouTubeProvider>(std::move(cfg), sink);
}

} // namespace tandem::chat
