// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - overlay server tests. Standalone runner: exits 0 when every check passes.
#include "overlay-server.h"

#include "chat-hub.h"

#include <json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
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
using socket_t = SOCKET;
constexpr socket_t kBad = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kBad = -1;
#endif

using namespace tandem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                       \
	do {                                                                              \
		++g_checks;                                                               \
		if (!(cond)) {                                                            \
			++g_failures;                                                     \
			fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                         \
	} while (0)

static void CloseSock(socket_t s)
{
#ifdef _WIN32
	closesocket(s);
#else
	close(s);
#endif
}

static void SetRecvTimeout(socket_t s, int ms)
{
#ifdef _WIN32
	DWORD t = static_cast<DWORD>(ms);
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&t), sizeof(t));
#else
	timeval tv{};
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

static socket_t Connect(int port)
{
	socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(static_cast<uint16_t>(port));
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
		CloseSock(s);
		return kBad;
	}
	SetRecvTimeout(s, 200);
	return s;
}

static bool SendStr(socket_t s, const std::string &d)
{
	size_t off = 0;
	while (off < d.size()) {
		auto n = send(s, d.data() + off, static_cast<int>(d.size() - off), 0);
		if (n <= 0)
			return false;
		off += static_cast<size_t>(n);
	}
	return true;
}

// Reads into buf until pred(buf) is true, EOF, or timeout. Returns true if pred matched.
// *eof is set when the peer closed the connection.
static bool ReadUntil(socket_t s, std::string &buf, const std::function<bool(const std::string &)> &pred, int timeout_ms,
		      bool *eof = nullptr)
{
	auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
	if (eof)
		*eof = false;
	char tmp[4096];
	while (!pred(buf)) {
		if (Clock::now() > deadline)
			return false;
		auto n = recv(s, tmp, sizeof(tmp), 0);
		if (n > 0) {
			buf.append(tmp, static_cast<size_t>(n));
			continue;
		}
		if (n == 0) {
			if (eof)
				*eof = true;
			return pred(buf);
		}
#ifdef _WIN32
		int e = WSAGetLastError();
		if (e != WSAETIMEDOUT && e != WSAEWOULDBLOCK) {
#else
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
#endif
			if (eof)
				*eof = true;
			return pred(buf);
		}
	}
	return true;
}

static bool Contains(const std::string &h, const std::string &n)
{
	return h.find(n) != std::string::npos;
}

// Sends a raw request and reads until the server closes. Returns the whole response.
static std::string RawRequest(int port, const std::string &raw, int timeout_ms = 3000)
{
	socket_t s = Connect(port);
	if (s == kBad)
		return {};
	SendStr(s, raw);
	std::string buf;
	ReadUntil(s, buf, [](const std::string &) { return false; }, timeout_ms);
	CloseSock(s);
	return buf;
}

static std::string Get(int port, const std::string &path, const std::string &method = "GET",
		       const std::string &host = "")
{
	std::string h = host.empty() ? "127.0.0.1:" + std::to_string(port) : host;
	return RawRequest(port, method + " " + path + " HTTP/1.1\r\nHost: " + h + "\r\nUser-Agent: test\r\n\r\n");
}

static int StatusCode(const std::string &resp)
{
	if (resp.size() < 12 || resp.rfind("HTTP/1.1 ", 0) != 0)
		return -1;
	return std::atoi(resp.c_str() + 9);
}

static std::string Body(const std::string &resp)
{
	size_t p = resp.find("\r\n\r\n");
	return p == std::string::npos ? std::string() : resp.substr(p + 4);
}

static chat::ChatEvent Msg(chat::Platform p, const std::string &id, const std::string &author, const std::string &text,
			   const std::string &author_id = "u1")
{
	chat::ChatEvent ev;
	ev.kind = chat::ChatEvent::Kind::Message;
	ev.platform = p;
	ev.message.platform = p;
	ev.message.id = id;
	ev.message.author = author;
	ev.message.author_id = author_id;
	ev.message.color = "#1E90FF";
	ev.message.badges = {"moderator"};
	ev.message.text = text;
	ev.message.timestamp_ms = 1700000000000;
	return ev;
}

struct SseEvent {
	std::string name;
	json data;
};

// Extracts complete SSE events (non-comment) from buf, consuming them.
static std::vector<SseEvent> TakeEvents(std::string &buf)
{
	std::vector<SseEvent> out;
	size_t p;
	while ((p = buf.find("\n\n")) != std::string::npos) {
		std::string block = buf.substr(0, p);
		buf.erase(0, p + 2);
		SseEvent ev;
		bool has_data = false;
		size_t pos = 0;
		while (pos <= block.size()) {
			size_t e = block.find('\n', pos);
			if (e == std::string::npos)
				e = block.size();
			std::string line = block.substr(pos, e - pos);
			if (line.rfind("event: ", 0) == 0)
				ev.name = line.substr(7);
			else if (line.rfind("data: ", 0) == 0) {
				ev.data = json::parse(line.substr(6), nullptr, false);
				has_data = true;
			}
			pos = e + 1;
		}
		if (has_data)
			out.push_back(std::move(ev));
	}
	return out;
}

// Opens /events and returns the socket with the HTTP head consumed into *rest.
static socket_t OpenEvents(int port, std::string *head, const std::string &query = "", std::string *rest = nullptr)
{
	socket_t s = Connect(port);
	if (s == kBad)
		return kBad;
	SendStr(s, "GET /events" + query + " HTTP/1.1\r\nHost: localhost:" + std::to_string(port) +
			   "\r\nAccept: text/event-stream\r\n\r\n");
	std::string buf;
	ReadUntil(s, buf, [](const std::string &b) { return Contains(b, "\r\n\r\n"); }, 2000);
	size_t p = buf.find("\r\n\r\n");
	if (head)
		*head = p == std::string::npos ? buf : buf.substr(0, p + 4);
	if (rest && p != std::string::npos)
		*rest = buf.substr(p + 4);
	return s;
}

static void TestBasics(int port)
{
	printf("basic routes\n");
	std::string r = Get(port, "/");
	CHECK(StatusCode(r) == 200);
	CHECK(Contains(r, "Content-Type: text/html; charset=utf-8"));
	CHECK(Contains(r, "X-Content-Type-Options: nosniff"));
	CHECK(Contains(r, "Content-Security-Policy: default-src 'none'; connect-src 'self'; style-src 'unsafe-inline'; "
			  "script-src 'unsafe-inline'"));
	CHECK(Contains(Body(r), "new EventSource("));
	CHECK(Contains(Body(r), "</html>"));
	CHECK(!Contains(Body(r), "innerHTML"));
	CHECK(!Contains(Body(r), "http://") && !Contains(Body(r), "https://"));

	r = Get(port, "/overlay?max=10&fade=0");
	CHECK(StatusCode(r) == 200);

	r = Get(port, "/", "HEAD");
	CHECK(StatusCode(r) == 200);
	CHECK(Body(r).empty());
	CHECK(Contains(r, "Content-Length: "));

	r = Get(port, "/health");
	CHECK(StatusCode(r) == 200);
	CHECK(Body(r) == "{\"ok\":true}");
	CHECK(Contains(r, "application/json"));

	r = Get(port, "/nope");
	CHECK(StatusCode(r) == 404);
	r = Get(port, "/favicon.ico");
	CHECK(StatusCode(r) == 404);

	r = RawRequest(port, "POST /health HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
				     "\r\nContent-Length: 0\r\n\r\n");
	CHECK(StatusCode(r) == 405);
	CHECK(Contains(r, "Allow: GET, HEAD"));
	r = Get(port, "/", "PUT");
	CHECK(StatusCode(r) == 405);

	r = RawRequest(port, "garbage\r\n\r\n");
	CHECK(StatusCode(r) == 400);
}

static void TestHostCheck(int port)
{
	printf("host header (DNS rebinding)\n");
	std::string p = std::to_string(port);
	CHECK(StatusCode(Get(port, "/health", "GET", "localhost:" + p)) == 200);
	CHECK(StatusCode(Get(port, "/health", "GET", "LOCALHOST:" + p)) == 200);
	CHECK(StatusCode(Get(port, "/health", "GET", "evil.example:" + p)) == 403);
	CHECK(StatusCode(Get(port, "/health", "GET", "127.0.0.1")) == 403);
	CHECK(StatusCode(Get(port, "/health", "GET", "127.0.0.1:1")) == 403);
	CHECK(StatusCode(Get(port, "/health", "GET", "127.0.0.1:" + p + ".evil.example")) == 403);
	CHECK(StatusCode(Get(port, "/events", "GET", "attacker.test:" + p)) == 403);
	CHECK(StatusCode(RawRequest(port, "GET /health HTTP/1.1\r\n\r\n")) == 403);
	CHECK(StatusCode(RawRequest(port, "GET /health HTTP/1.0\r\n\r\n")) == 403);
	CHECK(StatusCode(RawRequest(port, "GET /health HTTP/1.1\r\nHost: 127.0.0.1:" + p + "\r\nHost: evil:" + p +
						  "\r\n\r\n")) == 400);
}

static void TestLimits(int port)
{
	printf("request size and header timeout\n");
	std::string big = "GET / HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nX-Pad: " +
			  std::string(9000, 'a') + "\r\n\r\n";
	std::string r = RawRequest(port, big);
	CHECK(StatusCode(r) == 431);

	// Just under the limit still works.
	std::string ok = "GET /health HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nX-Pad: " +
			 std::string(7900, 'a') + "\r\n\r\n";
	CHECK(StatusCode(RawRequest(port, ok)) == 200);

	// Slow client: sends a partial head and stalls; server must give up (test timeout 600 ms).
	socket_t s = Connect(port);
	CHECK(s != kBad);
	SendStr(s, "GET / HTTP/1.1\r\n");
	std::string buf;
	bool eof = false;
	auto t0 = Clock::now();
	ReadUntil(s, buf, [](const std::string &) { return false; }, 3000, &eof);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
	CHECK(eof);
	CHECK(ms < 2000);
	CHECK(StatusCode(buf) == 408);
	CloseSock(s);

	// Idle connection with no bytes at all is closed too.
	s = Connect(port);
	buf.clear();
	ReadUntil(s, buf, [](const std::string &) { return false; }, 3000, &eof);
	CHECK(eof);
	CloseSock(s);
}

static void TestEvents(chat::ChatHub &hub, int port)
{
	printf("server-sent events\n");
	hub.ClearHistory();
	hub.Post(Msg(chat::Platform::Twitch, "h1", "old1", "history one"));
	hub.Post(Msg(chat::Platform::Kick, "h2", "old2", "history two"));
	hub.Post(Msg(chat::Platform::YouTube, "h3", "old3", "history three"));

	std::string head;
	std::string buf;
	socket_t s = OpenEvents(port, &head, "?max=2", &buf);
	CHECK(s != kBad);
	CHECK(StatusCode(head) == 200);
	CHECK(Contains(head, "Content-Type: text/event-stream"));
	CHECK(Contains(head, "Cache-Control: no-store"));
	CHECK(Contains(head, "X-Content-Type-Options: nosniff"));

	std::vector<SseEvent> got;
	auto collect = [&](size_t n, int timeout_ms) {
		ReadUntil(s, buf,
			  [&](const std::string &) {
				  for (auto &e : TakeEvents(buf))
					  got.push_back(std::move(e));
				  return got.size() >= n;
			  },
			  timeout_ms);
	};
	// 2 history messages + 3 status events.
	collect(5, 2000);
	CHECK(got.size() >= 5);
	if (got.size() >= 5) {
		CHECK(got[0].name == "message" && got[0].data["id"] == "h2" && got[0].data["platform"] == "kick");
		CHECK(got[1].name == "message" && got[1].data["id"] == "h3" && got[1].data["platform"] == "youtube");
		CHECK(got[1].data["platformName"] == "YouTube");
		CHECK(got[2].name == "status" && got[3].name == "status" && got[4].name == "status");
		CHECK(got[2].data["state"] == "stopped");
	}
	got.clear();

	const std::string evil = "<script>alert(1)</script> \"quoted\"\nsecond line \xF0\x9F\x98\x80";
	hub.Post(Msg(chat::Platform::Twitch, "m1", "Viewer", evil, "u42"));
	collect(1, 2000);
	CHECK(got.size() == 1);
	if (!got.empty()) {
		auto &d = got[0].data;
		CHECK(got[0].name == "message");
		CHECK(d["id"] == "m1");
		CHECK(d["platform"] == "twitch");
		CHECK(d["platformName"] == "Twitch");
		CHECK(d["author"] == "Viewer");
		CHECK(d["authorId"] == "u42");
		CHECK(d["color"] == "#1E90FF");
		CHECK(d["badges"].is_array() && d["badges"].size() == 1 && d["badges"][0] == "moderator");
		CHECK(d["text"] == evil);
		CHECK(d["ts"] == 1700000000000LL);
	}
	got.clear();

	// Invalid UTF-8 and a bad color must not break the stream.
	auto bad = Msg(chat::Platform::Kick, "m2", "Bad\xFF", "bytes \xC3\x28 here");
	bad.message.color = "red;background:url(x)";
	hub.Post(std::move(bad));
	collect(1, 2000);
	CHECK(got.size() == 1 && got[0].data.is_object() && got[0].data["color"] == "");
	got.clear();

	chat::ChatEvent del;
	del.kind = chat::ChatEvent::Kind::DeleteMessage;
	del.platform = chat::Platform::Twitch;
	del.target_id = "m1";
	hub.Post(std::move(del));
	chat::ChatEvent cu;
	cu.kind = chat::ChatEvent::Kind::ClearUser;
	cu.platform = chat::Platform::Kick;
	cu.target_id = "u9";
	hub.Post(std::move(cu));
	chat::ChatEvent ca;
	ca.kind = chat::ChatEvent::Kind::ClearAll;
	ca.platform = chat::Platform::YouTube;
	hub.Post(std::move(ca));
	chat::ChatEvent st;
	st.kind = chat::ChatEvent::Kind::Status;
	st.platform = chat::Platform::Kick;
	st.status.state = chat::ProviderState::Connected;
	st.status.detail = "ok";
	hub.Post(std::move(st));
	chat::ChatEvent quota;
	quota.kind = chat::ChatEvent::Kind::Quota;
	quota.quota_used = 5;
	hub.Post(std::move(quota));
	collect(4, 2000);
	CHECK(got.size() == 4);
	if (got.size() == 4) {
		CHECK(got[0].name == "delete" && got[0].data["platform"] == "twitch" && got[0].data["id"] == "m1");
		CHECK(got[1].name == "clearuser" && got[1].data["platform"] == "kick" && got[1].data["authorId"] == "u9");
		CHECK(got[2].name == "clearall" && got[2].data["platform"] == "youtube");
		CHECK(got[3].name == "status" && got[3].data["state"] == "connected" && got[3].data["detail"] == "ok");
	}

	// Heartbeat comment (test server uses 300 ms).
	std::string raw;
	ReadUntil(s, raw, [](const std::string &b) { return Contains(b, ": keepalive\n\n"); }, 2000);
	CHECK(Contains(raw, ": keepalive\n\n"));
	CloseSock(s);

	// HEAD /events returns headers only.
	std::string r = Get(port, "/events", "HEAD");
	CHECK(StatusCode(r) == 200 && Body(r).empty() && Contains(r, "text/event-stream"));
}

static void TestClientLimit(int port, int max_clients)
{
	printf("client limit\n");
	// Let the server notice the event stream closed by the previous test (probed every 250 ms).
	std::this_thread::sleep_for(std::chrono::milliseconds(600));
	std::vector<socket_t> socks;
	for (int i = 0; i < max_clients; ++i) {
		std::string head;
		socket_t s = OpenEvents(port, &head);
		CHECK(StatusCode(head) == 200);
		if (StatusCode(head) != 200)
			printf("  client %d got: %.40s\n", i, head.c_str());
		socks.push_back(s);
	}
	std::string r = Get(port, "/health");
	CHECK(StatusCode(r) == 503);

	// Closing one frees a slot once the server notices (it probes every 250 ms).
	CloseSock(socks.back());
	socks.pop_back();
	int code = 0;
	for (int i = 0; i < 20 && code != 200; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		code = StatusCode(Get(port, "/health"));
	}
	CHECK(code == 200);
	for (auto s : socks)
		CloseSock(s);
}

static void TestStopWithClients(chat::ChatHub &hub, const overlay::OverlayOptions &opt)
{
	printf("stop with connected clients\n");
	overlay::OverlayServer srv;
	std::string err;
	CHECK(srv.Start(&hub, opt, &err));
	int port = srv.port();
	std::vector<socket_t> socks;
	for (int i = 0; i < opt.max_clients - 1; ++i)
		socks.push_back(OpenEvents(port, nullptr));
	socket_t idle = Connect(port); // has not sent a request yet
	SendStr(idle, "GET / HTTP/1.1\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	auto t0 = Clock::now();
	srv.Stop();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
	printf("  Stop() took %lld ms with %d clients\n", static_cast<long long>(ms), opt.max_clients);
	CHECK(ms < 2000);
	CHECK(!srv.running());

	int closed = 0;
	for (auto s : socks) {
		std::string buf;
		bool eof = false;
		ReadUntil(s, buf, [](const std::string &) { return false; }, 1000, &eof);
		closed += eof ? 1 : 0;
		CloseSock(s);
	}
	CHECK(closed == static_cast<int>(socks.size()));
	CloseSock(idle);
	CHECK(Connect(port) == kBad);
}

static void TestPortInUse(chat::ChatHub &hub, const overlay::OverlayOptions &base)
{
	printf("port in use\n");
	overlay::OverlayServer a, b;
	std::string err;
	CHECK(a.Start(&hub, base, &err));
	auto opt = base;
	opt.port = a.port();
	err.clear();
	CHECK(!b.Start(&hub, opt, &err));
	CHECK(!b.running());
	CHECK(Contains(err, std::to_string(opt.port)) && Contains(err, "in use"));
	printf("  error text: %s\n", err.c_str());
	CHECK(!a.Start(&hub, base, &err)); // already running
	a.Stop();

	// A foreign listener on the port.
	socket_t l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	bind(l, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
	listen(l, 1);
#ifdef _WIN32
	int len = sizeof(addr);
#else
	socklen_t len = sizeof(addr);
#endif
	getsockname(l, reinterpret_cast<sockaddr *>(&addr), &len);
	opt.port = ntohs(addr.sin_port);
	err.clear();
	CHECK(!b.Start(&hub, opt, &err));
	CHECK(Contains(err, "in use"));
	CloseSock(l);

	// Invalid port.
	opt.port = 70000;
	CHECK(!b.Start(&hub, opt, &err));
}

static void TestRestartSamePort(chat::ChatHub &hub, const overlay::OverlayOptions &base)
{
	printf("restart on the same port after traffic\n");
	overlay::OverlayServer srv;
	std::string err;
	CHECK(srv.Start(&hub, base, &err));
	int port = srv.port();
	CHECK(StatusCode(Get(port, "/health")) == 200);
	CHECK(StatusCode(Get(port, "/")) == 200);
	socket_t ev = OpenEvents(port, nullptr);
	srv.Stop();
	CloseSock(ev);
	auto opt = base;
	opt.port = port;
	err.clear();
	bool ok = srv.Start(&hub, opt, &err);
	if (!ok)
		printf("  restart error: %s\n", err.c_str());
	CHECK(ok);
	CHECK(srv.port() == port);
	CHECK(StatusCode(Get(port, "/health")) == 200);
	srv.Stop();
}

static void Logger(chat::LogLevel level, const char *msg)
{
	if (level >= chat::LogLevel::Info)
		fprintf(stderr, "  [log] %s\n", msg);
}

int main()
{
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#else
	signal(SIGPIPE, SIG_IGN);
#endif
	chat::SetLogFunction(Logger);
	chat::ChatHub hub;

	overlay::OverlayOptions opt;
	opt.port = 0; // ephemeral
	opt.heartbeat_ms = 300;
	opt.header_timeout_ms = 600;
	opt.history_count = 50;
	opt.max_clients = 16;

	overlay::OverlayServer srv;
	std::string err;
	CHECK(srv.Start(&hub, opt, &err));
	CHECK(srv.running());
	int port = srv.port();
	CHECK(port > 0);
	printf("test server on 127.0.0.1:%d\n", port);

	TestBasics(port);
	TestHostCheck(port);
	TestLimits(port);
	TestEvents(hub, port);
	TestClientLimit(port, opt.max_clients);
	srv.Stop();
	CHECK(!srv.running());
	CHECK(srv.port() == 0);
	srv.Stop(); // idempotent

	TestStopWithClients(hub, opt);
	TestPortInUse(hub, opt);
	TestRestartSamePort(hub, opt);

#ifdef _WIN32
	WSACleanup();
#endif
	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
