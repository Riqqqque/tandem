// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - local overlay HTTP/SSE server. See overlay-server.h for the transport rationale.
//
// Threading: one acceptor thread plus one thread per accepted client (at most max_clients).
// Every socket is non-blocking and every wait is a short poll() slice that re-checks the stop
// flag, so Stop() can join everything quickly. Each thread closes only its own socket; Stop()
// never closes a socket another thread may still be using.
#include "overlay-server.h"
#include "overlay-page.h"

#include "chat-hub.h"
#include "emotes.h"

#include <json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <list>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace tandem::overlay {

using chat::ChatEvent;
using chat::ChatHub;
using chat::ChatMessage;
using chat::Log;
using chat::LogLevel;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
using io_len_t = int;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
using io_len_t = size_t;
#endif

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

constexpr size_t kMaxRequestBytes = 8192;
constexpr int kSliceMs = 100;
constexpr int kSendStallMs = 5000; // give up on a client that accepts no data for this long
constexpr size_t kMaxHistoryParam = 200;

int LastSocketError()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

bool WouldBlock(int e)
{
#ifdef _WIN32
	return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
	return e == EAGAIN || e == EWOULDBLOCK || e == EINTR;
#endif
}

void CloseSocket(socket_t s, bool abortive = false)
{
	if (s == kInvalidSocket)
		return;
	if (abortive) {
		// RST instead of FIN: no TIME_WAIT left behind on our port.
		linger lg{};
		lg.l_onoff = 1;
		lg.l_linger = 0;
		setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char *>(&lg), sizeof(lg));
	}
#ifdef _WIN32
	closesocket(s);
#else
	close(s);
#endif
}

bool SetNonBlocking(socket_t s)
{
#ifdef _WIN32
	u_long on = 1;
	return ioctlsocket(s, FIONBIO, &on) == 0;
#else
	int flags = fcntl(s, F_GETFL, 0);
	return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Returns >0 when ready, 0 on timeout, <0 on error.
int PollOne(socket_t s, short events, int timeout_ms)
{
	pollfd pfd{};
	pfd.fd = s;
	pfd.events = events;
#ifdef _WIN32
	return WSAPoll(&pfd, 1, timeout_ms);
#else
	int r = poll(&pfd, 1, timeout_ms);
	if (r < 0 && errno == EINTR)
		return 0;
	return r;
#endif
}

std::string SocketErrorText(int e)
{
#ifdef _WIN32
	char buf[256] = {};
	DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
				 static_cast<DWORD>(e), 0, buf, sizeof(buf), nullptr);
	std::string s(buf, n);
	while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '.'))
		s.pop_back();
	return s + " (" + std::to_string(e) + ")";
#else
	return std::string(strerror(e)) + " (" + std::to_string(e) + ")";
#endif
}

bool IsAddrInUse(int e)
{
#ifdef _WIN32
	return e == WSAEADDRINUSE || e == WSAEACCES;
#else
	return e == EADDRINUSE;
#endif
}

std::string ToLower(std::string s)
{
	for (auto &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string Trim(const std::string &s)
{
	size_t b = s.find_first_not_of(" \t");
	if (b == std::string::npos)
		return {};
	size_t e = s.find_last_not_of(" \t");
	return s.substr(b, e - b + 1);
}

bool IsHexColor(const std::string &c)
{
	if (c.size() != 7 || c[0] != '#')
		return false;
	return std::all_of(c.begin() + 1, c.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
}

std::string Dump(const json &j)
{
	// Provider text is not guaranteed to be valid UTF-8; replace instead of throwing.
	return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string SseEvent(const char *name, const json &data)
{
	std::string out = "event: ";
	out += name;
	out += "\ndata: ";
	out += Dump(data); // dump() escapes CR/LF, so the payload is always a single data line
	out += "\n\n";
	return out;
}

json MessageJson(const ChatMessage &m)
{
	json j;
	j["id"] = m.id;
	j["platform"] = chat::PlatformKey(m.platform);
	j["platformName"] = chat::PlatformName(m.platform);
	j["author"] = m.author;
	j["authorId"] = m.author_id;
	j["color"] = IsHexColor(m.color) ? m.color : std::string();
	j["badges"] = m.badges;
	j["text"] = m.text;
	if (!m.emotes.empty()) {
		// Text split into runs and emote images, so the page needs no byte/UTF-16 offset math.
		json parts = json::array();
		size_t pos = 0;
		for (const auto &e : m.emotes) {
			if (e.begin < pos || e.end > m.text.size() || !chat::IsTrustedEmoteUrl(e.url))
				continue;
			if (e.begin > pos)
				parts.push_back({{"t", m.text.substr(pos, e.begin - pos)}});
			parts.push_back({{"e", e.name}, {"u", e.url}});
			pos = e.end;
		}
		if (pos < m.text.size())
			parts.push_back({{"t", m.text.substr(pos)}});
		j["parts"] = parts;
	}
	j["ts"] = m.timestamp_ms;
	return j;
}

std::string FormatEvent(const ChatEvent &ev)
{
	switch (ev.kind) {
	case ChatEvent::Kind::Message:
		return SseEvent("message", MessageJson(ev.message));
	case ChatEvent::Kind::DeleteMessage:
		return SseEvent("delete", json{{"platform", chat::PlatformKey(ev.platform)}, {"id", ev.target_id}});
	case ChatEvent::Kind::ClearUser:
		return SseEvent("clearuser",
				json{{"platform", chat::PlatformKey(ev.platform)}, {"authorId", ev.target_id}});
	case ChatEvent::Kind::ClearAll:
		return SseEvent("clearall", json{{"platform", chat::PlatformKey(ev.platform)}});
	case ChatEvent::Kind::Status:
		return SseEvent("status", json{{"platform", chat::PlatformKey(ev.platform)},
					       {"platformName", chat::PlatformName(ev.platform)},
					       {"state", chat::ProviderStateName(ev.status.state)},
					       {"detail", ev.status.detail}});
	case ChatEvent::Kind::Quota:
	case ChatEvent::Kind::ChannelInfo:
		break;
	}
	return {};
}

struct Request {
	std::string method;
	std::string path;
	std::string query;
	std::string host;
	bool has_host = false;
};

bool ParseRequest(const std::string &head, Request &req)
{
	size_t eol = head.find("\r\n");
	if (eol == std::string::npos)
		return false;
	std::string line = head.substr(0, eol);
	size_t sp1 = line.find(' ');
	size_t sp2 = sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);
	if (sp1 == std::string::npos || sp2 == std::string::npos)
		return false;
	req.method = line.substr(0, sp1);
	std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
	std::string version = line.substr(sp2 + 1);
	if (version.rfind("HTTP/1.", 0) != 0 || target.empty())
		return false;
	size_t q = target.find('?');
	req.path = target.substr(0, q);
	if (q != std::string::npos)
		req.query = target.substr(q + 1);

	size_t pos = eol + 2;
	while (pos < head.size()) {
		size_t end = head.find("\r\n", pos);
		if (end == std::string::npos)
			end = head.size();
		if (end == pos)
			break;
		std::string h = head.substr(pos, end - pos);
		size_t colon = h.find(':');
		if (colon != std::string::npos && ToLower(Trim(h.substr(0, colon))) == "host") {
			if (req.has_host)
				return false; // duplicate Host is malformed
			req.has_host = true;
			req.host = ToLower(Trim(h.substr(colon + 1)));
		}
		pos = end + 2;
	}
	return true;
}

size_t QueryNumber(const std::string &query, const char *key, size_t def, size_t max_value)
{
	std::string k = std::string(key) + "=";
	size_t pos = 0;
	while (pos <= query.size()) {
		size_t amp = query.find('&', pos);
		if (amp == std::string::npos)
			amp = query.size();
		std::string part = query.substr(pos, amp - pos);
		if (part.rfind(k, 0) == 0) {
			std::string v = part.substr(k.size());
			if (v.empty() || v.size() > 6 || !std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; }))
				return def;
			return std::min(static_cast<size_t>(std::stoul(v)), max_value);
		}
		pos = amp + 1;
	}
	return def;
}

std::string Csp()
{
	return std::string("default-src 'none'; connect-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
			   "base-uri 'none'; form-action 'none'; img-src ") + chat::EmoteCspSources();
}

} // namespace

struct OverlayServer::Impl {
	struct Client {
		std::thread th;
		std::atomic<bool> done{false};
	};

	ChatHub *hub = nullptr;
	OverlayOptions opt;
	socket_t listen_sock = kInvalidSocket;
	int bound_port = 0;
	std::atomic<bool> stop{false};
	std::atomic<bool> is_running{false};
	std::thread acceptor;
	std::mutex mu;
	std::list<std::unique_ptr<Client>> clients;
	bool wsa_started = false;

	void AcceptLoop();
	void Reap(bool all);
	size_t ActiveClients();
	void RejectBusy(socket_t s);
	void Serve(socket_t s);
	bool SendAll(socket_t s, const char *data, size_t len);
	bool SendAll(socket_t s, const std::string &data) { return SendAll(s, data.data(), data.size()); }
	void FinishAndClose(socket_t s);
	void SendSimple(socket_t s, int code, const char *reason, const char *ctype, const std::string &body,
			bool head_only, const char *extra_headers = "");
	void ServeEvents(socket_t s, const Request &req);
	bool PeerClosed(socket_t s);
};

void OverlayServer::Impl::Reap(bool all)
{
	std::list<std::unique_ptr<Client>> finished;
	{
		std::lock_guard lock(mu);
		for (auto it = clients.begin(); it != clients.end();) {
			if (all || (*it)->done.load()) {
				finished.push_back(std::move(*it));
				it = clients.erase(it);
			} else {
				++it;
			}
		}
	}
	for (auto &c : finished)
		if (c->th.joinable())
			c->th.join();
}

size_t OverlayServer::Impl::ActiveClients()
{
	std::lock_guard lock(mu);
	return static_cast<size_t>(std::count_if(clients.begin(), clients.end(),
						 [](const std::unique_ptr<Client> &c) { return !c->done.load(); }));
}

void OverlayServer::Impl::AcceptLoop()
{
	while (!stop.load()) {
		int r = PollOne(listen_sock, POLLIN, 200);
		Reap(false);
		if (r <= 0)
			continue;
		sockaddr_in peer{};
#ifdef _WIN32
		int plen = sizeof(peer);
#else
		socklen_t plen = sizeof(peer);
#endif
		socket_t s = accept(listen_sock, reinterpret_cast<sockaddr *>(&peer), &plen);
		if (s == kInvalidSocket)
			continue;
		if (!SetNonBlocking(s)) {
			CloseSocket(s, true);
			continue;
		}
#ifdef SO_NOSIGPIPE
		int one = 1;
		setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
		if (ActiveClients() >= static_cast<size_t>(std::max(1, opt.max_clients))) {
			RejectBusy(s);
			continue;
		}
		auto c = std::make_unique<Client>();
		Client *raw = c.get();
		{
			std::lock_guard lock(mu);
			clients.push_back(std::move(c));
		}
		raw->th = std::thread([this, s, raw] {
			Serve(s);
			raw->done.store(true);
		});
	}
}

void OverlayServer::Impl::RejectBusy(socket_t s)
{
	// Runs on the acceptor thread, so keep it short: absorb what the client already sent (so
	// close() does not turn into a RST that eats the response), answer 503 and close.
	char buf[2048];
	auto deadline = Clock::now() + std::chrono::milliseconds(150);
	while (Clock::now() < deadline) {
		if (PollOne(s, POLLIN, 30) <= 0)
			break;
		auto n = recv(s, buf, sizeof(buf), 0);
		if (n <= 0)
			break;
		std::string_view seen(buf, static_cast<size_t>(n));
		if (seen.find("\r\n\r\n") != std::string_view::npos)
			break;
	}
	static const std::string resp = "HTTP/1.1 503 Service Unavailable\r\n"
					"Content-Type: text/plain; charset=utf-8\r\n"
					"Content-Length: 18\r\n"
					"Retry-After: 2\r\n"
					"X-Content-Type-Options: nosniff\r\n"
					"Cache-Control: no-store\r\n"
					"Connection: close\r\n\r\n"
					"too many clients\r\n";
	send(s, resp.data(), static_cast<io_len_t>(resp.size()), kSendFlags);
	Log(LogLevel::Debug, "[overlay] rejected connection: client limit reached");
	CloseSocket(s);
}

bool OverlayServer::Impl::SendAll(socket_t s, const char *data, size_t len)
{
	auto last_progress = Clock::now();
	while (len > 0) {
		if (stop.load())
			return false;
		auto n = send(s, data, static_cast<io_len_t>(std::min<size_t>(len, 1 << 20)), kSendFlags);
		if (n > 0) {
			data += n;
			len -= static_cast<size_t>(n);
			last_progress = Clock::now();
			continue;
		}
		if (n < 0 && !WouldBlock(LastSocketError()))
			return false;
		if (Clock::now() - last_progress > std::chrono::milliseconds(kSendStallMs))
			return false;
		if (PollOne(s, POLLOUT, kSliceMs) < 0)
			return false;
	}
	return true;
}

void OverlayServer::Impl::FinishAndClose(socket_t s)
{
	if (stop.load()) {
		CloseSocket(s, true);
		return;
	}
	// Half-close so the client sees EOF right away, then drain until it closes too (bounded)
	// so unread request bytes never turn the close into a RST that could eat the response.
#ifdef _WIN32
	shutdown(s, SD_SEND);
#else
	shutdown(s, SHUT_WR);
#endif
	char buf[512];
	auto deadline = Clock::now() + std::chrono::milliseconds(1000);
	while (!stop.load() && Clock::now() < deadline) {
		int r = PollOne(s, POLLIN, kSliceMs);
		if (r < 0)
			break;
		if (r == 0)
			continue;
		auto n = recv(s, buf, sizeof(buf), 0);
		if (n == 0)
			break;
		if (n < 0 && !WouldBlock(LastSocketError()))
			break;
	}
	CloseSocket(s, stop.load());
}

void OverlayServer::Impl::SendSimple(socket_t s, int code, const char *reason, const char *ctype,
				     const std::string &body, bool head_only, const char *extra_headers)
{
	std::string resp = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n";
	resp += "Content-Type: ";
	resp += ctype;
	resp += "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
	resp += "X-Content-Type-Options: nosniff\r\n"
		"Cache-Control: no-store\r\n"
		"Referrer-Policy: no-referrer\r\n"
		"Connection: close\r\n";
	resp += extra_headers;
	resp += "\r\n";
	if (!head_only)
		resp += body;
	SendAll(s, resp);
}

bool OverlayServer::Impl::PeerClosed(socket_t s)
{
	char buf[256];
	for (int i = 0; i < 16; ++i) {
		auto n = recv(s, buf, sizeof(buf), 0);
		if (n == 0)
			return true;
		if (n < 0)
			return !WouldBlock(LastSocketError());
		// Anything the client sends on an event stream is ignored.
	}
	return false;
}

void OverlayServer::Impl::Serve(socket_t s)
{
	// Read the request head (bounded in size and time).
	std::string head;
	bool complete = false, too_big = false;
	auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, opt.header_timeout_ms));
	char buf[2048];
	while (!stop.load() && Clock::now() < deadline) {
		int r = PollOne(s, POLLIN, kSliceMs);
		if (r < 0)
			break;
		if (r == 0)
			continue;
		auto n = recv(s, buf, sizeof(buf), 0);
		if (n == 0)
			break;
		if (n < 0) {
			if (WouldBlock(LastSocketError()))
				continue;
			break;
		}
		size_t old = head.size();
		head.append(buf, static_cast<size_t>(n));
		size_t from = old >= 3 ? old - 3 : 0;
		size_t end = head.find("\r\n\r\n", from);
		if (end != std::string::npos && end + 4 <= kMaxRequestBytes) {
			head.resize(end + 4);
			complete = true;
			break;
		}
		if (head.size() > kMaxRequestBytes) {
			too_big = true;
			break;
		}
	}

	if (stop.load()) {
		CloseSocket(s, true);
		return;
	}
	if (too_big) {
		SendSimple(s, 431, "Request Header Fields Too Large", "text/plain; charset=utf-8",
			   "request too large\n", false);
		FinishAndClose(s);
		return;
	}
	if (!complete) {
		if (!head.empty())
			SendSimple(s, 408, "Request Timeout", "text/plain; charset=utf-8", "request timeout\n", false);
		CloseSocket(s);
		return;
	}

	Request req;
	if (!ParseRequest(head, req)) {
		SendSimple(s, 400, "Bad Request", "text/plain; charset=utf-8", "bad request\n", false);
		FinishAndClose(s);
		return;
	}

	// DNS-rebinding protection: only accept the loopback names for our exact port.
	const std::string port_str = std::to_string(bound_port);
	bool head_only = req.method == "HEAD";
	if (!req.has_host || (req.host != "127.0.0.1:" + port_str && req.host != "localhost:" + port_str)) {
		SendSimple(s, 403, "Forbidden", "text/plain; charset=utf-8", "forbidden host\n", head_only);
		FinishAndClose(s);
		return;
	}
	if (req.method != "GET" && req.method != "HEAD") {
		SendSimple(s, 405, "Method Not Allowed", "text/plain; charset=utf-8", "method not allowed\n", false,
			   "Allow: GET, HEAD\r\n");
		FinishAndClose(s);
		return;
	}

	if (req.path == "/" || req.path == "/overlay") {
		std::string extra = std::string("Content-Security-Policy: ") + Csp() + "\r\n";
		SendSimple(s, 200, "OK", "text/html; charset=utf-8", kOverlayPage, head_only, extra.c_str());
		FinishAndClose(s);
	} else if (req.path == "/health") {
		SendSimple(s, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}", head_only);
		FinishAndClose(s);
	} else if (req.path == "/events") {
		if (head_only) {
			SendAll(s, "HTTP/1.1 200 OK\r\n"
				   "Content-Type: text/event-stream; charset=utf-8\r\n"
				   "Cache-Control: no-store\r\n"
				   "X-Content-Type-Options: nosniff\r\n"
				   "Connection: close\r\n\r\n");
			FinishAndClose(s);
		} else {
			ServeEvents(s, req);
			CloseSocket(s, stop.load());
		}
	} else {
		SendSimple(s, 404, "Not Found", "text/plain; charset=utf-8", "not found\n", head_only);
		FinishAndClose(s);
	}
}

void OverlayServer::Impl::ServeEvents(socket_t s, const Request &req)
{
	// Subscribe before reading history so nothing posted in between is lost. A message can then
	// appear twice (history and live); the page dedupes by platform + id.
	int sub = hub->Subscribe();
	Log(LogLevel::Debug, "[overlay] event stream opened");

	std::string out = "HTTP/1.1 200 OK\r\n"
			  "Content-Type: text/event-stream; charset=utf-8\r\n"
			  "Cache-Control: no-store\r\n"
			  "X-Content-Type-Options: nosniff\r\n"
			  "Connection: close\r\n\r\n"
			  "retry: 2000\n\n";

	size_t want = QueryNumber(req.query, "max", opt.history_count, kMaxHistoryParam);
	want = std::min(want, opt.history_count);
	std::vector<ChatMessage> history = hub->History();
	size_t first = history.size() > want ? history.size() - want : 0;
	for (size_t i = first; i < history.size(); ++i)
		out += SseEvent("message", MessageJson(history[i]));
	for (int p = 0; p < chat::kPlatformCount; ++p) {
		ChatEvent ev;
		ev.kind = ChatEvent::Kind::Status;
		ev.platform = static_cast<chat::Platform>(p);
		ev.status = hub->Status(ev.platform);
		out += FormatEvent(ev);
	}

	bool ok = SendAll(s, out);
	auto last_send = Clock::now();
	std::vector<ChatEvent> events;
	while (ok && !stop.load()) {
		hub->Wait(sub, std::chrono::milliseconds(250));
		if (stop.load())
			break;
		events.clear();
		hub->Drain(sub, events, 500);
		out.clear();
		for (const auto &ev : events)
			out += FormatEvent(ev);
		if (out.empty() && Clock::now() - last_send >= std::chrono::milliseconds(std::max(50, opt.heartbeat_ms)))
			out = ": keepalive\n\n";
		if (!out.empty()) {
			ok = SendAll(s, out);
			last_send = Clock::now();
		}
		if (ok && PeerClosed(s))
			break;
	}

	hub->Unsubscribe(sub);
	Log(LogLevel::Debug, "[overlay] event stream closed");
}

OverlayServer::OverlayServer() : impl_(std::make_unique<Impl>()) {}

OverlayServer::~OverlayServer()
{
	Stop();
}

bool OverlayServer::Start(chat::ChatHub *hub, const OverlayOptions &opt, std::string *err)
{
	auto fail = [&](const std::string &msg) {
		if (err)
			*err = msg;
		Log(LogLevel::Warning, "[overlay] %s", msg.c_str());
		return false;
	};
	if (impl_->is_running.load())
		return fail("overlay server is already running");
	if (!hub)
		return fail("overlay server needs a chat hub");
	if (opt.port < 0 || opt.port > 65535)
		return fail("overlay port must be between 1 and 65535");

	Impl &d = *impl_;
#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return fail("could not initialize Winsock");
	d.wsa_started = true;
#endif
	auto cleanup = [&] {
		CloseSocket(d.listen_sock);
		d.listen_sock = kInvalidSocket;
#ifdef _WIN32
		if (d.wsa_started)
			WSACleanup();
		d.wsa_started = false;
#endif
	};

	d.listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (d.listen_sock == kInvalidSocket) {
		std::string msg = "could not create socket: " + SocketErrorText(LastSocketError());
		cleanup();
		return fail(msg);
	}
	int one = 1;
#ifdef _WIN32
	// Nobody else may bind the same address while we hold it (prevents port hijacking).
	setsockopt(d.listen_sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&one), sizeof(one));
#else
	// On POSIX this only allows rebinding over TIME_WAIT; a live listener still blocks bind.
	setsockopt(d.listen_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<uint16_t>(opt.port));
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 only, never 0.0.0.0
	if (bind(d.listen_sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
		int e = LastSocketError();
		std::string msg = IsAddrInUse(e) ? "port " + std::to_string(opt.port) +
							   " on 127.0.0.1 is already in use; choose another overlay port"
						 : "could not bind 127.0.0.1:" + std::to_string(opt.port) + ": " +
							   SocketErrorText(e);
		cleanup();
		return fail(msg);
	}
	if (listen(d.listen_sock, 32) != 0 || !SetNonBlocking(d.listen_sock)) {
		std::string msg = "could not listen on 127.0.0.1:" + std::to_string(opt.port) + ": " +
				  SocketErrorText(LastSocketError());
		cleanup();
		return fail(msg);
	}
	sockaddr_in bound{};
#ifdef _WIN32
	int blen = sizeof(bound);
#else
	socklen_t blen = sizeof(bound);
#endif
	getsockname(d.listen_sock, reinterpret_cast<sockaddr *>(&bound), &blen);
	d.bound_port = ntohs(bound.sin_port);

	d.hub = hub;
	d.opt = opt;
	d.stop.store(false);
	d.is_running.store(true);
	d.acceptor = std::thread([&d] { d.AcceptLoop(); });
	Log(LogLevel::Info, "[overlay] listening on http://127.0.0.1:%d/", d.bound_port);
	return true;
}

void OverlayServer::Stop()
{
	Impl &d = *impl_;
	if (!d.is_running.load())
		return;
	d.stop.store(true);
	if (d.hub)
		d.hub->WakeAll();
	if (d.acceptor.joinable())
		d.acceptor.join();
	d.Reap(true);
	CloseSocket(d.listen_sock);
	d.listen_sock = kInvalidSocket;
#ifdef _WIN32
	if (d.wsa_started)
		WSACleanup();
	d.wsa_started = false;
#endif
	d.is_running.store(false);
	d.hub = nullptr;
	Log(LogLevel::Info, "[overlay] stopped");
}

bool OverlayServer::running() const
{
	return impl_->is_running.load();
}

int OverlayServer::port() const
{
	return impl_->is_running.load() ? impl_->bound_port : 0;
}

} // namespace tandem::overlay
