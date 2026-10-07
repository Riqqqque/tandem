// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - read-only Kick chat through Kick's public Pusher WebSocket.
#include "backoff.h"
#include "chat-util.h"
#include "net.h"
#include "parse-kick.h"
#include "provider-base.h"

#include <optional>
#include <set>

namespace tandem::chat {

namespace {

// Kick does not document these; they are what kick.com's own web client uses and can change
// without notice. Keep them here so an update is a one-line fix.
constexpr const char *kKickPusherUrl =
	"wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0&flash=false";
constexpr const char *kKickChannelApi = "https://kick.com/api/v2/channels/";

constexpr auto kSubscribeTimeout = std::chrono::seconds(20);
constexpr auto kPongTimeout = std::chrono::seconds(30);
constexpr auto kHealthyWindow = std::chrono::seconds(30);
constexpr auto kBlockedMinDelay = std::chrono::seconds(30);

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

class KickProvider final : public ProviderBase {
public:
	KickProvider(KickChatConfig cfg, ChatSink *sink) : ProviderBase(Platform::Kick, sink), cfg_(std::move(cfg)) {}
	~KickProvider() override { Stop(); }

protected:
	void Run() override;

private:
	enum class End { Stop, Retry, ServerReconnect, Fatal };
	std::optional<std::string> ResolveChatroom(const std::string &slug);
	End RunSession(const std::string &chatroom_id, std::string &reason);
	void WarnOnce(const std::string &event, const std::string &why);

	KickChatConfig cfg_;
	RecentIds seen_{500};
	Backoff backoff_;
	std::set<std::string> warned_;
};

void KickProvider::WarnOnce(const std::string &event, const std::string &why)
{
	if (warned_.insert(event).second)
		Log(LogLevel::Warning, "[chat/kick] could not parse event '%s': %s", event.c_str(), why.c_str());
}

std::optional<std::string> KickProvider::ResolveChatroom(const std::string &slug)
{
	const HttpHeaders headers = {
		"Accept: application/json, text/plain, */*",
		"Accept-Language: en-US,en;q=0.9",
	};
	while (!stopping()) {
		PostStatus(ProviderState::Connecting, "Looking up kick.com/" + slug);
		HttpResponse r = HttpGet(kKickChannelApi + slug, headers, 15000, stop_flag());
		if (stopping())
			return std::nullopt;
		auto now = Clock::now();
		if (r.error.empty() && r.status == 200) {
			if (auto id = ParseKickChannelChatroomId(r.body)) {
				Log(LogLevel::Info, "[chat/kick] %s -> chatroom %s", slug.c_str(), id->c_str());
				backoff_.Reset();
				PostChannelInfo(ParseKickChannelUserId(r.body));
				return id;
			}
			auto delay = backoff_.Next(now);
			PostStatus(ProviderState::Reconnecting,
				   "Unexpected channel lookup response from Kick; you can enter the chatroom ID "
				   "manually, " + RetryText(delay));
			if (!WaitFor(delay))
				return std::nullopt;
			continue;
		}
		if (r.status == 404) {
			PostStatus(ProviderState::Error, "Kick channel '" + slug + "' was not found");
			return std::nullopt;
		}
		milliseconds delay;
		std::string why;
		if (r.status == 403 || r.status == 429 || r.status == 503) {
			auto min_delay = std::max<milliseconds>(kBlockedMinDelay, std::chrono::seconds(r.retry_after_s));
			delay = backoff_.Next(now, min_delay);
			why = "Kick blocked the channel lookup (HTTP " + std::to_string(r.status) +
			      "). You can enter the chatroom ID manually in the Kick settings";
		} else {
			delay = backoff_.Next(now, std::chrono::seconds(r.retry_after_s));
			why = r.error.empty() ? "Channel lookup failed (HTTP " + std::to_string(r.status) + ")"
					      : "Channel lookup failed (" + r.error + ")";
		}
		PostStatus(ProviderState::Reconnecting, why + ", " + RetryText(delay));
		if (!WaitFor(delay))
			return std::nullopt;
	}
	return std::nullopt;
}

void KickProvider::Run()
{
	backoff_.Reset();

	// Report configuration mistakes before environment problems.
	std::string chatroom_id(TrimAscii(cfg_.chatroom_id));
	std::string slug(TrimAscii(cfg_.channel));
	if (!chatroom_id.empty() ? !IsValidKickChatroomId(chatroom_id) : !IsValidKickSlug(slug)) {
		PostStatus(ProviderState::Error,
			   !chatroom_id.empty() ? "Invalid Kick chatroom ID (digits only)" : "Invalid Kick channel name");
		return;
	}
	if (!WebSocketsAvailable()) {
		PostStatus(ProviderState::Error, "libcurl was built without TLS support, so chat cannot connect");
		return;
	}

	if (chatroom_id.empty()) {
		auto id = ResolveChatroom(slug);
		if (!id)
			return; // stopped, or a permanent error was posted
		chatroom_id = *id;
	}

	std::optional<Clock::time_point> last_server_reconnect;
	while (!stopping()) {
		PostStatus(ProviderState::Connecting, "chatroom " + chatroom_id);
		std::string reason;
		End end = RunSession(chatroom_id, reason);
		if (end == End::Stop || stopping())
			return;
		if (end == End::Fatal) {
			PostStatus(ProviderState::Error, reason);
			return;
		}
		milliseconds delay;
		auto now = Clock::now();
		if (end == End::ServerReconnect &&
		    (!last_server_reconnect || now - *last_server_reconnect > kHealthyWindow)) {
			last_server_reconnect = now;
			delay = milliseconds(250);
		} else {
			delay = backoff_.Next(now);
		}
		PostStatus(ProviderState::Reconnecting, reason + ", " + RetryText(delay));
		if (!WaitFor(delay))
			return;
	}
}

KickProvider::End KickProvider::RunSession(const std::string &chatroom_id, std::string &reason)
{
	const std::string pusher_channel = "chatrooms." + chatroom_id + ".v2";
	WebSocket ws;
	ws.SetCancelFlag(stop_flag());
	std::string err;
	if (!ws.Connect(kKickPusherUrl, {}, 15000, &err)) {
		if (stopping())
			return End::Stop;
		reason = "Connection failed: " + err;
		return End::Retry;
	}

	bool established = false;
	bool subscribed = false;
	auto activity_timeout = std::chrono::seconds(120);
	auto subscribe_deadline = Clock::now() + kSubscribeTimeout;
	auto last_rx = Clock::now();
	std::optional<Clock::time_point> ping_sent;

	for (;;) {
		if (stopping()) {
			ws.Close();
			return End::Stop;
		}
		auto r = ws.Recv(250);
		auto now = Clock::now();
		using T = WebSocket::RecvResult::Type;
		if (r.type == T::Closed || r.type == T::Error) {
			if (stopping())
				return End::Stop;
			reason = "Disconnected (" + r.data + ")";
			return End::Retry;
		}
		if (r.type == T::Text) {
			last_rx = now;
			KickEvent ev = ParseKickPusher(r.data, NowMs());
			using Ty = KickEvent::Type;
			switch (ev.type) {
			case Ty::ConnectionEstablished:
				established = true;
				activity_timeout = std::chrono::seconds(ev.activity_timeout_s);
				if (!ws.SendText(KickSubscribeMessage(chatroom_id))) {
					reason = "Failed to subscribe";
					return End::Retry;
				}
				break;
			case Ty::SubscriptionSucceeded:
				if (ev.channel == pusher_channel && !subscribed) {
					subscribed = true;
					backoff_.MarkConnected(now);
					PostStatus(ProviderState::Connected, "chatroom " + chatroom_id);
				}
				break;
			case Ty::Ping:
				if (!ws.SendText(KickPongMessage())) {
					reason = "Failed to answer ping";
					return End::Retry;
				}
				break;
			case Ty::Pong:
				ping_sent.reset();
				break;
			case Ty::PusherError:
				// Pusher protocol: 4000-4099 do not reconnect, 4100-4199 back off, 4200-4299 reconnect now.
				if (ev.error_code >= 4000 && ev.error_code < 4100) {
					ws.Close();
					reason = "Kick chat refused the connection (code " + std::to_string(ev.error_code) +
						 (ev.error_text.empty() ? "" : ": " + ev.error_text) + ")";
					return End::Fatal;
				}
				if (ev.error_code >= 4100 && ev.error_code < 4300) {
					ws.Close();
					reason = "Kick chat closed the connection (code " + std::to_string(ev.error_code) + ")";
					return ev.error_code >= 4200 ? End::ServerReconnect : End::Retry;
				}
				Log(LogLevel::Warning, "[chat/kick] pusher error %d: %s", ev.error_code, ev.error_text.c_str());
				break;
			case Ty::Message:
				if ((ev.channel.empty() || ev.channel == pusher_channel) && seen_.Insert(ev.message.id))
					PostChatMessage(std::move(ev.message));
				break;
			case Ty::DeleteMessage:
				PostDelete(ev.target_id);
				break;
			case Ty::ClearUser:
				PostClearUser(ev.target_id);
				break;
			case Ty::ClearAll:
				PostClearAll();
				break;
			case Ty::Malformed:
				WarnOnce(ev.event_name.empty() ? std::string("(unparseable)") : ev.event_name, ev.error_text);
				break;
			case Ty::Unknown:
			case Ty::None:
				break;
			}
		}

		if (!subscribed && now > subscribe_deadline) {
			ws.Close();
			reason = established ? "Timed out subscribing to the chatroom" : "Timed out waiting for Kick chat";
			return End::Retry;
		}
		if (!ping_sent && now - last_rx > activity_timeout) {
			if (!ws.SendText(KickPingMessage())) {
				reason = "Failed to send ping";
				return End::Retry;
			}
			ping_sent = now;
		}
		if (ping_sent && now - *ping_sent > kPongTimeout) {
			ws.Close();
			reason = "Ping timeout";
			return End::Retry;
		}
	}
}

} // namespace

std::unique_ptr<ChatProvider> CreateKickProvider(KickChatConfig cfg, ChatSink *sink)
{
	return std::make_unique<KickProvider>(std::move(cfg), sink);
}

} // namespace tandem::chat
