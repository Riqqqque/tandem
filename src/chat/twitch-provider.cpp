// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - anonymous, read-only Twitch chat over IRC-over-WebSocket.
#include "backoff.h"
#include "chat-util.h"
#include "net.h"
#include "parse-twitch.h"
#include "provider-base.h"

#include <optional>
#include <random>

namespace tandem::chat {

namespace {

constexpr const char *kTwitchIrcUrl = "wss://irc-ws.chat.twitch.tv:443";
// Twitch's documented anonymous login: any password with a justinfan<digits> nick.
constexpr const char *kAnonymousPass = "SCHMOOPIIE";
constexpr auto kIdleBeforePing = std::chrono::milliseconds(270000); // 4.5 minutes
constexpr auto kPongTimeout = std::chrono::seconds(10);
constexpr auto kJoinTimeout = std::chrono::seconds(20);
constexpr auto kHealthyWindow = std::chrono::seconds(30);

using Clock = std::chrono::steady_clock;

class TwitchProvider final : public ProviderBase {
public:
	TwitchProvider(TwitchChatConfig cfg, ChatSink *sink) : ProviderBase(Platform::Twitch, sink), cfg_(std::move(cfg))
	{
	}
	~TwitchProvider() override { Stop(); }

protected:
	void Run() override;

private:
	enum class End { Stop, Retry, ServerReconnect, Fatal };
	End RunSession(const std::string &channel, std::string &reason);
	void HandleLine(const std::string &channel, std::string_view line, WebSocket &ws, bool &joined,
			std::optional<Clock::time_point> &ping_sent, End &end, std::string &reason);

	TwitchChatConfig cfg_;
	RecentIds seen_{500};
	Backoff backoff_;
	std::string nick_;
	bool welcomed_ = false;      // 001 received on the current connection
	int silent_join_failures_ = 0; // consecutive join timeouts on welcomed connections
};

void TwitchProvider::Run()
{
	const std::string channel = NormalizeTwitchChannel(cfg_.channel);
	if (!IsValidTwitchChannel(channel)) {
		PostStatus(ProviderState::Error, "Invalid Twitch channel name (1-25 letters, digits or _)");
		return;
	}
	if (!CurlSupportsWss()) {
		PostStatus(ProviderState::Error, "libcurl was built without WebSocket support");
		return;
	}

	backoff_.Reset();
	silent_join_failures_ = 0;
	std::optional<Clock::time_point> last_server_reconnect;
	while (!stopping()) {
		PostStatus(ProviderState::Connecting, "#" + channel);
		std::string reason;
		End end = RunSession(channel, reason);
		if (end == End::Stop || stopping())
			return;
		if (end == End::Fatal) {
			PostStatus(ProviderState::Error, reason);
			return;
		}

		std::chrono::milliseconds delay(0);
		auto now = Clock::now();
		if (end == End::ServerReconnect &&
		    (!last_server_reconnect || now - *last_server_reconnect > kHealthyWindow)) {
			// Planned maintenance: reconnect at once, without growing the backoff.
			last_server_reconnect = now;
			delay = std::chrono::milliseconds(250);
		} else {
			delay = backoff_.Next(now);
		}
		PostStatus(ProviderState::Reconnecting, reason + ", " + RetryText(delay));
		if (!WaitFor(delay))
			return;
	}
}

TwitchProvider::End TwitchProvider::RunSession(const std::string &channel, std::string &reason)
{
	WebSocket ws;
	ws.SetCancelFlag(stop_flag());
	std::string err;
	if (!ws.Connect(kTwitchIrcUrl, {}, 15000, &err)) {
		if (stopping())
			return End::Stop;
		reason = "Connection failed: " + err;
		return End::Retry;
	}

	{
		std::mt19937 gen{std::random_device{}()};
		std::uniform_int_distribution<int> dist(10000, 99999);
		nick_ = "justinfan" + std::to_string(dist(gen));
	}
	if (!ws.SendText("CAP REQ :twitch.tv/tags twitch.tv/commands") ||
	    !ws.SendText(std::string("PASS ") + kAnonymousPass) || !ws.SendText("NICK " + nick_) ||
	    !ws.SendText("JOIN #" + channel)) {
		if (stopping())
			return End::Stop;
		reason = "Failed to send login";
		return End::Retry;
	}

	bool joined = false;
	welcomed_ = false;
	auto join_deadline = Clock::now() + kJoinTimeout;
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
			End end = End::Retry;
			reason.clear();
			std::string_view data = r.data;
			while (!data.empty()) {
				size_t nl = data.find('\n');
				std::string_view line = data.substr(0, nl);
				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);
				if (!line.empty()) {
					HandleLine(channel, line, ws, joined, ping_sent, end, reason);
					if (!reason.empty()) {
						ws.Close();
						return end;
					}
				}
				if (nl == std::string_view::npos)
					break;
				data.remove_prefix(nl + 1);
			}
		}

		if (!joined && now > join_deadline) {
			ws.Close();
			// Twitch silently ignores a JOIN for a channel that does not exist: the login
			// succeeds but no JOIN echo, ROOMSTATE or NOTICE follows. Two such timeouts in a
			// row on otherwise healthy connections are treated as a permanent error.
			if (welcomed_ && ++silent_join_failures_ >= 2) {
				reason = "Twitch did not confirm joining #" + channel +
					 "; the channel may not exist or may be suspended";
				return End::Fatal;
			}
			reason = "Timed out joining #" + channel;
			return End::Retry;
		}
		if (!ping_sent && now - last_rx > kIdleBeforePing) {
			if (!ws.SendText("PING :tandem")) {
				reason = "Failed to send ping";
				return End::Retry;
			}
			ping_sent = now;
		}
		if (ping_sent && now - *ping_sent > kPongTimeout) {
			reason = "Ping timeout";
			return End::Retry;
		}
	}
}

void TwitchProvider::HandleLine(const std::string &channel, std::string_view line, WebSocket &ws, bool &joined,
				std::optional<Clock::time_point> &ping_sent, End &end, std::string &reason)
{
	TwitchEvent ev = ParseTwitchLine(line, NowMs());
	using Ty = TwitchEvent::Type;
	auto mark_joined = [&] {
		if (joined)
			return;
		joined = true;
		silent_join_failures_ = 0;
		backoff_.MarkConnected(Clock::now());
		PostStatus(ProviderState::Connected, "#" + channel);
	};

	switch (ev.type) {
	case Ty::Ping:
		if (!ws.SendText(ev.param.empty() ? std::string("PONG") : "PONG :" + ev.param)) {
			reason = "Failed to answer ping";
			end = End::Retry;
		}
		break;
	case Ty::Pong:
		ping_sent.reset();
		break;
	case Ty::Reconnect:
		reason = "Twitch requested a reconnect";
		end = End::ServerReconnect;
		break;
	case Ty::Notice:
		if (IsPermanentTwitchNotice(ev)) {
			reason = ev.param.empty() ? "Twitch refused the channel" : ev.param;
			if (!ev.notice_id.empty())
				reason += " (" + ev.notice_id + ")";
			end = End::Fatal;
		} else {
			Log(LogLevel::Info, "[chat/twitch] notice %s: %s", ev.notice_id.c_str(), ev.param.c_str());
		}
		break;
	case Ty::Join:
		if (ev.channel == channel && ev.nick == nick_)
			mark_joined();
		break;
	case Ty::RoomState:
		if (ev.channel == channel)
			mark_joined();
		break;
	case Ty::Message:
		if (ev.channel == channel && seen_.Insert(ev.message.id))
			PostChatMessage(std::move(ev.message));
		break;
	case Ty::DeleteMessage:
		if (ev.channel == channel)
			PostDelete(ev.target_id);
		break;
	case Ty::ClearUser:
		if (ev.channel == channel && !ev.target_id.empty())
			PostClearUser(ev.target_id);
		break;
	case Ty::ClearAll:
		if (ev.channel == channel)
			PostClearAll();
		break;
	case Ty::Welcome:
		welcomed_ = true;
		break;
	case Ty::None:
		break;
	}
}

} // namespace

std::unique_ptr<ChatProvider> CreateTwitchProvider(TwitchChatConfig cfg, ChatSink *sink)
{
	return std::make_unique<TwitchProvider>(std::move(cfg), sink);
}

} // namespace tandem::chat
