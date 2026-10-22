// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - read-only YouTube live chat without an API key, through the same public web chat
// youtube.com shows next to a stream. Like Kick's feed it is unofficial, so YouTube can change it.
#include "backoff.h"
#include "chat-util.h"
#include "provider-base.h"
#include "youtube-web.h"

#include <algorithm>
#include <unordered_set>

namespace tandem::chat {

namespace {

constexpr const char *kBrowserUserAgent =
	"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
	"Chrome/140.0.0.0 Safari/537.36";
constexpr const char *kGetLiveChatUrl = "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?prettyPrint=false";
// The canonical link sits roughly 0.8 MB into a channel page; the channel id follows soon after.
constexpr size_t kMaxLivePageBytes = 4 * 1024 * 1024;
constexpr size_t kChannelIdSearchBytes = 400 * 1024;

constexpr auto kWaitForLive = std::chrono::seconds(30);
constexpr auto kMinPoll = std::chrono::milliseconds(1000);
constexpr auto kMaxPoll = std::chrono::milliseconds(5000);
constexpr int kMaxFailuresBeforeReload = 3;
// While reading a channel's stream, how often its /live page is checked for the stream ending
// (the newest-messages request can keep answering for a while after a stream is over).
constexpr auto kLiveRecheck = std::chrono::minutes(2);

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

std::string FindChannelId(std::string_view html, size_t from)
{
	constexpr std::string_view kKey = "\"channelId\":\"";
	size_t p = html.find(kKey, from);
	if (p == std::string_view::npos)
		return {};
	std::string_view id = html.substr(p + kKey.size(), 24);
	return IsValidYouTubeChannelId(id) ? std::string(id) : std::string();
}

} // namespace

const HttpHeaders &YouTubeWebHeaders()
{
	static const HttpHeaders headers = {
		kBrowserUserAgent,
		"Accept-Language: en-US,en;q=0.9",
		"Cookie: SOCS=CAI",
	};
	return headers;
}

YouTubeLiveLookup LookUpYouTubeLive(const YouTubeTarget &target, const std::atomic<bool> *cancel)
{
	YouTubeLiveLookup out;
	std::string page;
	std::optional<std::string> canonical;
	size_t canonical_at = 0;
	auto on_chunk = [&](std::string_view chunk) {
		page.append(chunk);
		if (!canonical) {
			canonical = FindCanonicalVideoId(page);
			if (canonical)
				canonical_at = page.find("rel=\"canonical\"");
		}
		if (canonical) {
			if (canonical->empty())
				return false; // not live: nothing else needed
			if (!FindChannelId(page, canonical_at).empty() || page.size() > canonical_at + kChannelIdSearchBytes)
				return false;
		}
		return page.size() < kMaxLivePageBytes;
	};
	HttpResponse r = HttpStream(YouTubeLivePageUrl(target), YouTubeWebHeaders(), on_chunk, cancel, 20000, true);
	out.retry_after_s = r.retry_after_s;
	if (r.status == 404) {
		out.result = YouTubeLiveLookup::Result::NotFound;
		return out;
	}
	if (!canonical) {
		out.result = YouTubeLiveLookup::Result::Error;
		out.error = r.status == 0 ? (r.error.empty() ? "no response" : r.error)
					  : r.status != 200 ? "HTTP " + std::to_string(r.status)
							    : "unexpected page";
		return out;
	}
	if (canonical->empty()) {
		out.result = YouTubeLiveLookup::Result::NotLive;
		return out;
	}
	out.result = YouTubeLiveLookup::Result::Live;
	out.video_id = *canonical;
	out.channel_id = FindChannelId(page, canonical_at);
	if (target.channel_path.rfind("channel/", 0) == 0)
		out.channel_id = target.channel_path.substr(8);
	return out;
}

YouTubeChatPageResult FetchYouTubeChatPage(const std::string &video_id, const std::atomic<bool> *cancel)
{
	YouTubeChatPageResult out;
	HttpResponse r = HttpGet("https://www.youtube.com/live_chat?is_popout=1&v=" + video_id, YouTubeWebHeaders(), 20000,
				 cancel, true);
	out.retry_after_s = r.retry_after_s;
	if (!r.error.empty() || r.status != 200) {
		out.error = r.status == 0 ? r.error : "HTTP " + std::to_string(r.status);
		return out;
	}
	out.fetched = true;
	out.info = ParseYouTubeLiveChatHtml(r.body);
	return out;
}

namespace {

class YouTubeWebProvider final : public ProviderBase {
public:
	YouTubeWebProvider(YouTubeTarget target, ChatSink *sink)
		: ProviderBase(Platform::YouTube, sink),
		  target_(std::move(target))
	{
	}
	~YouTubeWebProvider() override { Stop(); }

protected:
	void Run() override;

private:
	// Returns the live video id, or "" when stopping or on a permanent error (status posted).
	std::string WaitForLiveVideo();
	// Reads one stream's chat until it ends. Returns false on a permanent error or stop.
	bool ReadChat(const std::string &video_id);
	bool Retry(const std::string &reason, long retry_after_s = 0);

	YouTubeTarget target_;
	std::string channel_id_;
	bool video_only_ = false; // the user gave a video, not a channel
	RecentIds seen_{5000};
	RecentIds seen_actions_{2000};
	Backoff backoff_;
};

bool YouTubeWebProvider::Retry(const std::string &reason, long retry_after_s)
{
	auto delay = backoff_.Next(Clock::now(), std::chrono::seconds(retry_after_s));
	PostStatus(ProviderState::Reconnecting, reason + ", " + RetryText(delay));
	return WaitFor(delay);
}

void YouTubeWebProvider::Run()
{
	if (target_.kind == YouTubeTarget::Kind::Invalid) {
		PostStatus(ProviderState::Error, "Enter your YouTube channel (@handle or link) or a live video link");
		return;
	}
	video_only_ = target_.kind == YouTubeTarget::Kind::Video;
	backoff_.Reset();
	while (!stopping()) {
		std::string video = video_only_ ? target_.video_id : WaitForLiveVideo();
		if (video.empty() || stopping())
			return;
		if (!ReadChat(video))
			return;
		if (video_only_) {
			PostStatus(ProviderState::Error, "This live stream has ended");
			return;
		}
		// The stream ended; wait for the channel's next one.
		Log(LogLevel::Info, "[chat/youtube] stream %s ended; waiting for the next one", video.c_str());
	}
}

std::string YouTubeWebProvider::WaitForLiveVideo()
{
	while (!stopping()) {
		PostStatus(ProviderState::Connecting, "Looking up " + target_.label);
		YouTubeLiveLookup r = LookUpYouTubeLive(target_, stop_flag());
		if (stopping())
			return {};
		switch (r.result) {
		case YouTubeLiveLookup::Result::Live:
			backoff_.Reset();
			if (!r.channel_id.empty() && r.channel_id != channel_id_) {
				channel_id_ = r.channel_id;
				PostChannelInfo(channel_id_);
			}
			return r.video_id;
		case YouTubeLiveLookup::Result::NotLive:
			backoff_.Reset();
			PostStatus(ProviderState::Waiting, target_.label + " isn't live yet; checking every 30 seconds");
			if (!WaitFor(kWaitForLive))
				return {};
			break;
		case YouTubeLiveLookup::Result::NotFound:
			if (target_.could_be_video) {
				// A bare 11-character word that isn't a handle: try it as a video id.
				video_only_ = true;
				return target_.video_id;
			}
			PostStatus(ProviderState::Error, "YouTube channel " + target_.label + " was not found");
			return {};
		case YouTubeLiveLookup::Result::Error:
			if (!Retry("Couldn't reach YouTube (" + r.error + ")", r.retry_after_s))
				return {};
			break;
		}
	}
	return {};
}

bool YouTubeWebProvider::ReadChat(const std::string &video_id)
{
	YouTubeWebChatInfo info;
	int failures = 0;
	bool first = true;
	milliseconds interval(2000);
	std::unordered_set<std::string> last_batch;
	auto next_recheck = Clock::now() + kLiveRecheck;

	auto load_page = [&]() -> int { // 1 ok, 0 retry, -1 give up
		PostStatus(ProviderState::Connecting, "Opening the live chat of " + video_id);
		YouTubeChatPageResult p = FetchYouTubeChatPage(video_id, stop_flag());
		if (stopping())
			return -1;
		if (!p.fetched)
			return Retry("Couldn't open the live chat (" + p.error + ")", p.retry_after_s) ? 0 : -1;
		if (!p.info.has_chat) {
			std::string why = p.info.message.empty() ? "This video has no live chat" : p.info.message;
			if (video_only_) {
				PostStatus(ProviderState::Error, why);
				return -1;
			}
			// Scheduled with chat off, or just ended: go back to waiting for the channel.
			PostStatus(ProviderState::Waiting, why + " Checking again in 30 seconds.");
			return WaitFor(kWaitForLive) ? 2 : -1;
		}
		info = std::move(p.info);
		return 1;
	};

	for (;;) {
		int rc = load_page();
		if (rc == 1)
			break;
		if (rc == 2)
			return true;
		if (rc < 0)
			return false;
	}

	while (!stopping()) {
		HttpHeaders headers = YouTubeWebHeaders();
		headers.emplace_back("Content-Type: application/json");
		HttpResponse r = HttpPost(kGetLiveChatUrl, headers, BuildYouTubeGetLiveChatBody(info.client_version, info.continuation),
					  15000, stop_flag(), false);
		if (stopping())
			return false;
		YouTubeWebChatBatch batch;
		if (r.error.empty() && r.status == 200)
			batch = ParseYouTubeGetLiveChat(r.body, NowMs());
		if (!batch.ok) {
			++failures;
			std::string why = r.status == 0 ? r.error : "HTTP " + std::to_string(r.status);
			if (r.status == 200)
				why = "unexpected response";
			if (failures >= kMaxFailuresBeforeReload) {
				// The page's continuation or client version may have gone stale.
				failures = 0;
				if (!Retry("Live chat interrupted (" + why + ")", r.retry_after_s))
					return false;
				for (;;) {
					int rc = load_page();
					if (rc == 1)
						break;
					if (rc == 2)
						return true;
					if (rc < 0)
						return false;
				}
				first = true;
				continue;
			}
			if (!WaitFor(interval))
				return false;
			continue;
		}
		failures = 0;
		if (batch.ended)
			return true;

		// Each response holds the newest messages; post the ones not seen yet. The first one
		// is history from before Tandem connected, which isn't shown (same as Twitch and Kick).
		size_t fresh = 0, overlap = 0, messages = 0;
		std::unordered_set<std::string> this_batch;
		for (const auto &it : batch.items) {
			switch (it.kind) {
			case YouTubeChatItem::Kind::Message: {
				++messages;
				this_batch.insert(it.message.id);
				if (last_batch.count(it.message.id))
					++overlap;
				if (!seen_.Insert(it.message.id) || first)
					break;
				++fresh;
				ChatMessage m = it.message;
				PostChatMessage(std::move(m));
				break;
			}
			// Every response repeats recent moderation actions too: apply each one once, and
			// not the ones that happened before Tandem connected.
			case YouTubeChatItem::Kind::Delete:
				if (seen_actions_.Insert("d:" + it.target_id) && !first)
					PostDelete(it.target_id);
				break;
			case YouTubeChatItem::Kind::ClearUser:
				if (seen_actions_.Insert("u:" + it.target_id) && !first)
					PostClearUser(it.target_id);
				break;
			case YouTubeChatItem::Kind::ChatEnded:
				return true;
			}
		}
		if (first) {
			first = false;
			backoff_.MarkConnected(Clock::now());
			Log(LogLevel::Info, "[chat/youtube] reading the live chat of %s", video_id.c_str());
			PostStatus(ProviderState::Connected, "Live chat of " + video_id);
		} else if (messages > 0 && overlap == 0 && !last_batch.empty()) {
			Log(LogLevel::Debug, "[chat/youtube] chat is very busy; some messages may be skipped");
		}
		last_batch = std::move(this_batch);

		// Busy chats are read more often so no message falls out of the newest-messages window.
		if (fresh >= 30 || (messages > 0 && overlap == 0))
			interval = kMinPoll;
		else if (fresh >= 8)
			interval = milliseconds(2000);
		else if (fresh == 0)
			interval = std::min<milliseconds>(kMaxPoll, interval + milliseconds(1000));
		else
			interval = milliseconds(3000);
		if (!video_only_ && Clock::now() >= next_recheck) {
			next_recheck = Clock::now() + kLiveRecheck;
			YouTubeLiveLookup live = LookUpYouTubeLive(target_, stop_flag());
			if (stopping())
				return false;
			if (live.result == YouTubeLiveLookup::Result::NotLive ||
			    (live.result == YouTubeLiveLookup::Result::Live && live.video_id != video_id))
				return true;
		}
		if (!WaitFor(interval))
			return false;
	}
	return false;
}

} // namespace

std::unique_ptr<ChatProvider> CreateYouTubeWebProvider(const std::string &channel_or_video, ChatSink *sink)
{
	return std::make_unique<YouTubeWebProvider>(ParseYouTubeTarget(channel_or_video), sink);
}

} // namespace tandem::chat
