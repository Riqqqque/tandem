// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include "net.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#ifndef TANDEM_CHAT_VERSION
#define TANDEM_CHAT_VERSION "0.1"
#endif

namespace tandem::chat {

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kMaxBodyBytes = 32 * 1024 * 1024;
constexpr size_t kMaxErrorBodyBytes = 1024 * 1024;

struct CurlDeleter {
	void operator()(CURL *c) const { curl_easy_cleanup(c); }
};
struct SlistDeleter {
	void operator()(curl_slist *l) const { curl_slist_free_all(l); }
};
using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;
using SlistPtr = std::unique_ptr<curl_slist, SlistDeleter>;

curl_slist *BuildHeaders(const HttpHeaders &headers)
{
	curl_slist *list = nullptr;
	for (const auto &h : headers) {
		curl_slist *next = curl_slist_append(list, h.c_str());
		if (!next)
			break;
		list = next;
	}
	return list;
}

struct ProgressCtx {
	const std::atomic<bool> *cancel = nullptr;
	bool aborted_by_cancel = false;
	// HttpStream idle detection / WebSocket connect deadline
	long idle_timeout_ms = 0;
	Clock::time_point last_data = Clock::now();
	bool idle_hit = false;
	Clock::time_point deadline = Clock::time_point::max();
	bool deadline_hit = false;
};

int XferInfo(void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto *ctx = static_cast<ProgressCtx *>(p);
	if (ctx->cancel && ctx->cancel->load()) {
		ctx->aborted_by_cancel = true;
		return 1;
	}
	auto now = Clock::now();
	if (ctx->idle_timeout_ms > 0 && now - ctx->last_data > std::chrono::milliseconds(ctx->idle_timeout_ms)) {
		ctx->idle_hit = true;
		return 1;
	}
	if (now > ctx->deadline) {
		ctx->deadline_hit = true;
		return 1;
	}
	return 0;
}

void CommonOptions(CURL *c, const char *url, curl_slist *hdrs, ProgressCtx *progress, char *errbuf,
		   bool follow_redirects)
{
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, DefaultUserAgent());
	if (hdrs)
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, XferInfo);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, progress);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(c, CURLOPT_VERBOSE, 0L);
#if LIBCURL_VERSION_NUM >= 0x075700
	// Do not wait for a stuck DNS resolver thread when a transfer is aborted on Stop().
	curl_easy_setopt(c, CURLOPT_QUICK_EXIT, 1L);
#endif
}

std::string ErrorText(CURLcode rc, const char *errbuf)
{
	if (errbuf && errbuf[0])
		return errbuf;
	return curl_easy_strerror(rc);
}

struct BodyCtx {
	std::string *body;
	size_t limit;
	bool overflow = false;
};

size_t WriteBody(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *ctx = static_cast<BodyCtx *>(userdata);
	size_t n = size * nmemb;
	if (ctx->body->size() + n > ctx->limit) {
		ctx->overflow = true;
		return 0;
	}
	ctx->body->append(ptr, n);
	return n;
}

} // namespace

void EnsureCurlInit()
{
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

bool CurlSupportsWss()
{
	EnsureCurlInit();
	const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
	if (!info || !info->protocols)
		return false;
	for (const char *const *p = info->protocols; *p; ++p) {
		if (std::strcmp(*p, "wss") == 0)
			return true;
	}
	return false;
}

namespace {

// TANDEM_FORCE_RAW_WS=1 exercises the fallback WebSocket even when libcurl has its own.
bool ForceRawWebSocket()
{
	static const bool force = [] {
		const char *v = std::getenv("TANDEM_FORCE_RAW_WS");
		return v && *v && *v != '0';
	}();
	return force;
}

bool CurlHasTls()
{
	EnsureCurlInit();
	const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
	return info && (info->features & CURL_VERSION_SSL);
}

} // namespace

bool WebSocketsAvailable()
{
	return CurlSupportsWss() || CurlHasTls();
}

const char *DefaultUserAgent()
{
	return "Tandem/" TANDEM_CHAT_VERSION;
}

HttpResponse HttpGet(const std::string &url, const HttpHeaders &headers, long timeout_ms,
		     const std::atomic<bool> *cancel, bool follow_redirects)
{
	EnsureCurlInit();
	HttpResponse res;
	CurlPtr c(curl_easy_init());
	if (!c) {
		res.error = "curl_easy_init failed";
		return res;
	}
	SlistPtr hdrs(BuildHeaders(headers));
	ProgressCtx progress;
	progress.cancel = cancel;
	char errbuf[CURL_ERROR_SIZE] = {};
	CommonOptions(c.get(), url.c_str(), hdrs.get(), &progress, errbuf, follow_redirects);
	curl_easy_setopt(c.get(), CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(c.get(), CURLOPT_TIMEOUT_MS, timeout_ms);
	curl_easy_setopt(c.get(), CURLOPT_CONNECTTIMEOUT_MS, std::min<long>(timeout_ms, 15000));
	curl_easy_setopt(c.get(), CURLOPT_ACCEPT_ENCODING, "");
	BodyCtx body{&res.body, kMaxBodyBytes};
	curl_easy_setopt(c.get(), CURLOPT_WRITEFUNCTION, WriteBody);
	curl_easy_setopt(c.get(), CURLOPT_WRITEDATA, &body);

	CURLcode rc = curl_easy_perform(c.get());
	curl_easy_getinfo(c.get(), CURLINFO_RESPONSE_CODE, &res.status);
	curl_off_t retry_after = 0;
	if (curl_easy_getinfo(c.get(), CURLINFO_RETRY_AFTER, &retry_after) == CURLE_OK && retry_after > 0)
		res.retry_after_s = static_cast<long>(std::min<curl_off_t>(retry_after, 86400));
	if (rc != CURLE_OK) {
		res.cancelled = progress.aborted_by_cancel;
		if (res.cancelled)
			res.error = "cancelled";
		else if (body.overflow)
			res.error = "response too large";
		else
			res.error = ErrorText(rc, errbuf);
	}
	return res;
}

namespace {

struct StreamCtx {
	CURL *curl;
	const ChunkFn *on_chunk;
	ProgressCtx *progress;
	HttpResponse *res;
	bool stopped_by_callback = false;
	bool error_overflow = false;
};

size_t WriteStream(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *ctx = static_cast<StreamCtx *>(userdata);
	size_t n = size * nmemb;
	ctx->progress->last_data = Clock::now();
	long status = 0;
	curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &status);
	if (status >= 300 || status == 0) {
		if (ctx->res->body.size() + n > kMaxErrorBodyBytes) {
			ctx->error_overflow = true;
			return 0;
		}
		ctx->res->body.append(ptr, n);
		return n;
	}
	if (!(*ctx->on_chunk)(std::string_view(ptr, n))) {
		ctx->stopped_by_callback = true;
		return 0;
	}
	return n;
}

} // namespace

HttpResponse HttpStream(const std::string &url, const HttpHeaders &headers, const ChunkFn &on_chunk,
			const std::atomic<bool> *cancel, long idle_timeout_ms, bool follow_redirects)
{
	EnsureCurlInit();
	HttpResponse res;
	CurlPtr c(curl_easy_init());
	if (!c) {
		res.error = "curl_easy_init failed";
		return res;
	}
	SlistPtr hdrs(BuildHeaders(headers));
	ProgressCtx progress;
	progress.cancel = cancel;
	progress.idle_timeout_ms = idle_timeout_ms;
	char errbuf[CURL_ERROR_SIZE] = {};
	CommonOptions(c.get(), url.c_str(), hdrs.get(), &progress, errbuf, follow_redirects);
	curl_easy_setopt(c.get(), CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(c.get(), CURLOPT_CONNECTTIMEOUT_MS, 15000L);
	curl_easy_setopt(c.get(), CURLOPT_ACCEPT_ENCODING, "");
	StreamCtx sctx{c.get(), &on_chunk, &progress, &res};
	curl_easy_setopt(c.get(), CURLOPT_WRITEFUNCTION, WriteStream);
	curl_easy_setopt(c.get(), CURLOPT_WRITEDATA, &sctx);

	CURLcode rc = curl_easy_perform(c.get());
	curl_easy_getinfo(c.get(), CURLINFO_RESPONSE_CODE, &res.status);
	curl_off_t retry_after = 0;
	if (curl_easy_getinfo(c.get(), CURLINFO_RETRY_AFTER, &retry_after) == CURLE_OK && retry_after > 0)
		res.retry_after_s = static_cast<long>(std::min<curl_off_t>(retry_after, 86400));
	if (rc != CURLE_OK) {
		if (progress.aborted_by_cancel || sctx.stopped_by_callback) {
			res.cancelled = true;
			res.error = "cancelled";
		} else if (progress.idle_hit) {
			res.idle_timeout = true;
			res.error = "stream idle timeout";
		} else if (sctx.error_overflow) {
			res.error = "error response too large";
		} else {
			res.error = ErrorText(rc, errbuf);
		}
	}
	return res;
}

// ---------------------------------------------------------------------------------------------
// WebSocket

WebSocket::WebSocket() = default;

WebSocket::~WebSocket()
{
	Release();
}

void WebSocket::Release()
{
	if (curl_) {
		curl_easy_cleanup(static_cast<CURL *>(curl_));
		curl_ = nullptr;
	}
	if (headers_) {
		curl_slist_free_all(static_cast<curl_slist *>(headers_));
		headers_ = nullptr;
	}
	message_.clear();
	frame_done_ = true;
	control_frame_ = false;
	close_frame_ = false;
	close_payload_.clear();
	ready_.clear();
	closed_ = false;
	close_reason_.clear();
	raw_ = false;
	rx_.clear();
}

bool WebSocket::Connect(const std::string &url, const HttpHeaders &headers, long timeout_ms, std::string *err)
{
	Release();
	EnsureCurlInit();
	if (buf_.empty())
		buf_.resize(64 * 1024);
	if (ForceRawWebSocket() || !CurlSupportsWss())
		return ConnectRaw(url, headers, timeout_ms, err);

	CURL *c = curl_easy_init();
	if (!c) {
		if (err)
			*err = "curl_easy_init failed";
		return false;
	}
	curl_ = c;
	headers_ = BuildHeaders(headers);

	ProgressCtx progress;
	progress.cancel = cancel_;
	progress.deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
	char errbuf[CURL_ERROR_SIZE] = {};
	CommonOptions(c, url.c_str(), static_cast<curl_slist *>(headers_), &progress, errbuf, false);
	curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);

	CURLcode rc = curl_easy_perform(c);

	// The progress context and error buffer live on this stack frame only.
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, nullptr);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, nullptr);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, nullptr);

	if (rc != CURLE_OK) {
		long status = 0;
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
		if (err) {
			if (progress.aborted_by_cancel)
				*err = "cancelled";
			else if (progress.deadline_hit)
				*err = "connection timed out";
			else
				*err = ErrorText(rc, errbuf);
			if (status && status != 101)
				*err += " (HTTP " + std::to_string(status) + ")";
		}
		Release();
		return false;
	}
	return true;
}

bool WebSocket::WaitSocket(bool for_write, int timeout_ms)
{
	if (!curl_)
		return false;
	curl_socket_t sock = CURL_SOCKET_BAD;
	if (curl_easy_getinfo(static_cast<CURL *>(curl_), CURLINFO_ACTIVESOCKET, &sock) != CURLE_OK ||
	    sock == CURL_SOCKET_BAD) {
		std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, timeout_ms)));
		return false;
	}
#ifdef _WIN32
	WSAPOLLFD pfd{};
	pfd.fd = sock;
	pfd.events = for_write ? POLLWRNORM : POLLRDNORM;
	int r = WSAPoll(&pfd, 1, timeout_ms);
#else
	pollfd pfd{};
	pfd.fd = sock;
	pfd.events = for_write ? POLLOUT : POLLIN;
	int r = poll(&pfd, 1, timeout_ms);
#endif
	return r > 0;
}

bool WebSocket::SendText(std::string_view text)
{
	if (!curl_ || closed_)
		return false;
	if (raw_)
		return RawSendFrame(0x1, text.data(), text.size());
	auto deadline = Clock::now() + std::chrono::seconds(10);
	size_t off = 0;
	while (off < text.size()) {
		if (Cancelled() || Clock::now() > deadline)
			return false;
		size_t sent = 0;
		CURLcode rc = curl_ws_send(static_cast<CURL *>(curl_), text.data() + off, text.size() - off, &sent, 0,
					   CURLWS_TEXT);
		if (rc == CURLE_OK) {
			off += sent;
			if (sent == 0 && off < text.size())
				WaitSocket(true, 100);
		} else if (rc == CURLE_AGAIN) {
			// libcurl 8.12 keeps the frame buffered; retry with the same remaining data.
			off += sent;
			WaitSocket(true, 100);
		} else {
			return false;
		}
	}
	return true;
}

WebSocket::RecvResult WebSocket::Recv(int timeout_ms)
{
	using Type = RecvResult::Type;
	auto take_ready = [this]() {
		RecvResult r{Type::Text, std::move(ready_.front())};
		ready_.pop_front();
		return r;
	};
	if (!ready_.empty())
		return take_ready();
	if (closed_)
		return {Type::Closed, close_reason_};
	if (!curl_)
		return {Type::Error, "not connected"};
	if (raw_)
		return RawRecv(timeout_ms);

	CURL *c = static_cast<CURL *>(curl_);
	auto deadline = Clock::now() + std::chrono::milliseconds(std::max(0, timeout_ms));

	// libcurl 8.12 reports CURLWS_CONT only for continuation opcodes and does not expose FIN,
	// while newer versions set it on non-final fragments. A message is therefore treated as
	// complete once its last frame is fully read and either no more data is immediately
	// available or the next frame starts a new message.
	for (;;) {
		if (Cancelled())
			return {Type::Error, "cancelled"};

		size_t n = 0;
		const curl_ws_frame *meta = nullptr;
		CURLcode rc = curl_ws_recv(c, buf_.data(), buf_.size(), &n, &meta);

		if (rc == CURLE_AGAIN) {
			if (!message_.empty() && frame_done_ && !control_frame_) {
				RecvResult r{Type::Text, std::move(message_)};
				message_.clear();
				return r;
			}
			auto remaining =
				std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
			if (remaining <= 0)
				return {Type::Timeout, {}};
			WaitSocket(false, static_cast<int>(std::min<long long>(remaining, 250)));
			continue;
		}
		if (rc != CURLE_OK) {
			closed_ = true;
			close_reason_ = rc == CURLE_GOT_NOTHING ? "connection closed by server" : curl_easy_strerror(rc);
			if (!message_.empty() && frame_done_) {
				ready_.push_back(std::move(message_));
				message_.clear();
			}
			if (!ready_.empty())
				return take_ready();
			return {rc == CURLE_GOT_NOTHING ? Type::Closed : Type::Error, close_reason_};
		}
		if (!meta)
			continue;

		const int flags = meta->flags;
		const bool frame_start = meta->offset == 0;
		const bool frame_end = meta->bytesleft == 0;

		if (flags & (CURLWS_PING | CURLWS_PONG)) {
			// Answered by libcurl's auto-pong; nothing to deliver.
			control_frame_ = !frame_end;
			continue;
		}
		if (flags & CURLWS_CLOSE) {
			if (frame_start)
				close_payload_.clear();
			close_payload_.append(buf_.data(), n);
			control_frame_ = !frame_end;
			if (!frame_end)
				continue;
			int code = 0;
			std::string reason;
			if (close_payload_.size() >= 2) {
				code = (static_cast<unsigned char>(close_payload_[0]) << 8) |
				       static_cast<unsigned char>(close_payload_[1]);
				reason = close_payload_.substr(2, 120);
			}
			// Echo the close frame (best effort), as the protocol expects.
			size_t sent = 0;
			curl_ws_send(c, close_payload_.data(), std::min<size_t>(close_payload_.size(), 2), &sent, 0,
				     CURLWS_CLOSE);
			closed_ = true;
			close_reason_ = "closed by server";
			if (code) {
				close_reason_ += " (code " + std::to_string(code);
				if (!reason.empty())
					close_reason_ += ": " + reason;
				close_reason_ += ")";
			}
			if (!message_.empty() && frame_done_) {
				ready_.push_back(std::move(message_));
				message_.clear();
			}
			if (!ready_.empty())
				return take_ready();
			return {Type::Closed, close_reason_};
		}

		control_frame_ = false;
		const bool continuation = (flags & CURLWS_CONT) && !(flags & (CURLWS_TEXT | CURLWS_BINARY));
		if (frame_start && !continuation && !message_.empty()) {
			// A new message begins; the buffered one is complete.
			RecvResult r{Type::Text, std::move(message_)};
			message_.assign(buf_.data(), n);
			frame_done_ = frame_end;
			return r;
		}
		message_.append(buf_.data(), n);
		frame_done_ = frame_end;
		if (message_.size() > kMaxMessageBytes) {
			message_.clear();
			closed_ = true;
			close_reason_ = "message too large";
			return {Type::Error, close_reason_};
		}
	}
}

void WebSocket::Close()
{
	if (curl_ && !closed_) {
		const unsigned char payload[2] = {0x03, 0xE8}; // 1000 normal closure
		if (raw_) {
			RawSendFrame(0x8, payload, sizeof(payload));
		} else {
			size_t sent = 0;
			curl_ws_send(static_cast<CURL *>(curl_), payload, sizeof(payload), &sent, 0, CURLWS_CLOSE);
		}
	}
	Release();
}

// ---------------------------------------------------------------------------------------------
// Fallback WebSocket (RFC 6455) over a libcurl CONNECT_ONLY TLS connection

namespace {

std::string Base64(const unsigned char *data, size_t len)
{
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (size_t i = 0; i < len; i += 3) {
		uint32_t v = static_cast<uint32_t>(data[i]) << 16;
		if (i + 1 < len)
			v |= static_cast<uint32_t>(data[i + 1]) << 8;
		if (i + 2 < len)
			v |= data[i + 2];
		out += tbl[(v >> 18) & 63];
		out += tbl[(v >> 12) & 63];
		out += i + 1 < len ? tbl[(v >> 6) & 63] : '=';
		out += i + 2 < len ? tbl[v & 63] : '=';
	}
	return out;
}

void RandomBytes(unsigned char *out, size_t n)
{
	static thread_local std::mt19937 rng(std::random_device{}());
	for (size_t i = 0; i < n; ++i)
		out[i] = static_cast<unsigned char>(rng() & 0xFF);
}

struct WsUrl {
	std::string host;
	std::string port = "443";
	std::string path = "/";
};

bool ParseWssUrl(const std::string &url, WsUrl *out)
{
	const std::string scheme = "wss://";
	if (url.compare(0, scheme.size(), scheme) != 0)
		return false;
	std::string rest = url.substr(scheme.size());
	size_t slash = rest.find('/');
	size_t query = rest.find('?');
	size_t cut = std::min(slash, query);
	std::string authority = rest.substr(0, cut);
	if (cut != std::string::npos)
		out->path = rest[cut] == '/' ? rest.substr(cut) : "/" + rest.substr(cut);
	size_t colon = authority.rfind(':');
	if (colon != std::string::npos && authority.find(']') == std::string::npos) {
		out->host = authority.substr(0, colon);
		out->port = authority.substr(colon + 1);
	} else {
		out->host = authority;
	}
	return !out->host.empty();
}

} // namespace

bool WebSocket::ConnectRaw(const std::string &url, const HttpHeaders &headers, long timeout_ms, std::string *err)
{
	WsUrl u;
	if (!ParseWssUrl(url, &u)) {
		if (err)
			*err = "unsupported WebSocket URL";
		return false;
	}

	CURL *c = curl_easy_init();
	if (!c) {
		if (err)
			*err = "curl_easy_init failed";
		return false;
	}
	curl_ = c;
	raw_ = true;

	ProgressCtx progress;
	progress.cancel = cancel_;
	progress.deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
	char errbuf[CURL_ERROR_SIZE] = {};
	std::string https = "https://" + u.host + ":" + u.port + "/";
	CommonOptions(c, https.c_str(), nullptr, &progress, errbuf, false);
	curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 1L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, nullptr);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, nullptr);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, nullptr);
	auto fail = [&](const std::string &msg) {
		if (err)
			*err = msg;
		Release();
		return false;
	};
	if (rc != CURLE_OK)
		return fail(progress.aborted_by_cancel ? "cancelled"
			    : progress.deadline_hit    ? "connection timed out"
						       : ErrorText(rc, errbuf));

	unsigned char nonce[16];
	RandomBytes(nonce, sizeof(nonce));
	std::string request = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
			      (u.port == "443" ? "" : ":" + u.port) +
			      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
			      "Sec-WebSocket-Key: " +
			      Base64(nonce, sizeof(nonce)) + "\r\nUser-Agent: " + DefaultUserAgent() + "\r\n";
	for (const auto &h : headers)
		request += h + "\r\n";
	request += "\r\n";
	if (!RawSendAll(request.data(), request.size(), static_cast<int>(timeout_ms)))
		return fail("WebSocket handshake could not be sent");

	// Read the response head. The server is already authenticated by TLS, so the handshake is
	// accepted on "101"; Sec-WebSocket-Accept is not re-derived here.
	auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
	size_t head_end;
	while ((head_end = rx_.find("\r\n\r\n")) == std::string::npos) {
		if (Cancelled())
			return fail("cancelled");
		if (Clock::now() > deadline)
			return fail("connection timed out");
		if (rx_.size() > 16 * 1024)
			return fail("invalid WebSocket handshake response");
		size_t n = 0;
		rc = curl_easy_recv(c, buf_.data(), buf_.size(), &n);
		if (rc == CURLE_AGAIN) {
			WaitSocket(false, 100);
			continue;
		}
		if (rc != CURLE_OK || n == 0)
			return fail("connection closed during WebSocket handshake");
		rx_.append(buf_.data(), n);
	}
	std::string status_line = rx_.substr(0, rx_.find("\r\n"));
	if (status_line.find(" 101") == std::string::npos) {
		size_t sp = status_line.find(' ');
		std::string code = sp == std::string::npos ? "" : status_line.substr(sp + 1, 3);
		return fail("WebSocket handshake refused (HTTP " + code + ")");
	}
	rx_.erase(0, head_end + 4);
	return true;
}

bool WebSocket::RawSendAll(const void *data, size_t len, int timeout_ms)
{
	auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
	const char *p = static_cast<const char *>(data);
	while (len > 0) {
		if (Cancelled() || Clock::now() > deadline)
			return false;
		size_t sent = 0;
		CURLcode rc = curl_easy_send(static_cast<CURL *>(curl_), p, len, &sent);
		if (rc == CURLE_AGAIN) {
			WaitSocket(true, 100);
			continue;
		}
		if (rc != CURLE_OK)
			return false;
		p += sent;
		len -= sent;
	}
	return true;
}

bool WebSocket::RawSendFrame(int opcode, const void *data, size_t len)
{
	std::string frame;
	frame.reserve(len + 14);
	frame += static_cast<char>(0x80 | (opcode & 0x0F)); // FIN + opcode
	if (len < 126) {
		frame += static_cast<char>(0x80 | len);
	} else if (len <= 0xFFFF) {
		frame += static_cast<char>(0x80 | 126);
		frame += static_cast<char>((len >> 8) & 0xFF);
		frame += static_cast<char>(len & 0xFF);
	} else {
		frame += static_cast<char>(0x80 | 127);
		for (int i = 7; i >= 0; --i)
			frame += static_cast<char>((static_cast<uint64_t>(len) >> (8 * i)) & 0xFF);
	}
	unsigned char mask[4];
	RandomBytes(mask, sizeof(mask)); // client frames must be masked
	frame.append(reinterpret_cast<const char *>(mask), 4);
	const auto *src = static_cast<const unsigned char *>(data);
	for (size_t i = 0; i < len; ++i)
		frame += static_cast<char>(src[i] ^ mask[i & 3]);
	return RawSendAll(frame.data(), frame.size(), 10000);
}

WebSocket::RecvResult WebSocket::RawRecv(int timeout_ms)
{
	using Type = RecvResult::Type;
	CURL *c = static_cast<CURL *>(curl_);
	auto deadline = Clock::now() + std::chrono::milliseconds(std::max(0, timeout_ms));
	for (;;) {
		if (Cancelled())
			return {Type::Error, "cancelled"};

		// Parse every complete frame already buffered.
		while (rx_.size() >= 2) {
			const auto *b = reinterpret_cast<const unsigned char *>(rx_.data());
			bool fin = (b[0] & 0x80) != 0;
			int opcode = b[0] & 0x0F;
			bool masked = (b[1] & 0x80) != 0;
			uint64_t len = b[1] & 0x7F;
			size_t pos = 2;
			if (len == 126) {
				if (rx_.size() < 4)
					break;
				len = (static_cast<uint64_t>(b[2]) << 8) | b[3];
				pos = 4;
			} else if (len == 127) {
				if (rx_.size() < 10)
					break;
				len = 0;
				for (int i = 0; i < 8; ++i)
					len = (len << 8) | b[2 + i];
				pos = 10;
			}
			if (len > kMaxMessageBytes) {
				closed_ = true;
				close_reason_ = "message too large";
				return {Type::Error, close_reason_};
			}
			size_t mask_pos = pos;
			if (masked)
				pos += 4;
			if (rx_.size() < pos + len)
				break;
			std::string payload = rx_.substr(pos, static_cast<size_t>(len));
			if (masked) {
				for (size_t i = 0; i < payload.size(); ++i)
					payload[i] = static_cast<char>(payload[i] ^ rx_[mask_pos + (i & 3)]);
			}
			rx_.erase(0, pos + static_cast<size_t>(len));

			if (opcode == 0x9) { // ping
				RawSendFrame(0xA, payload.data(), payload.size());
				continue;
			}
			if (opcode == 0xA) // pong
				continue;
			if (opcode == 0x8) { // close
				int code = payload.size() >= 2 ? (static_cast<unsigned char>(payload[0]) << 8) |
									 static_cast<unsigned char>(payload[1])
							       : 0;
				RawSendFrame(0x8, payload.data(), std::min<size_t>(payload.size(), 2));
				closed_ = true;
				close_reason_ = "closed by server";
				if (code) {
					close_reason_ += " (code " + std::to_string(code);
					if (payload.size() > 2)
						close_reason_ += ": " + payload.substr(2, 120);
					close_reason_ += ")";
				}
				return {Type::Closed, close_reason_};
			}
			// text (1), binary (2) or continuation (0)
			message_ += payload;
			if (message_.size() > kMaxMessageBytes) {
				closed_ = true;
				close_reason_ = "message too large";
				return {Type::Error, close_reason_};
			}
			if (fin) {
				RecvResult r{Type::Text, std::move(message_)};
				message_.clear();
				return r;
			}
		}

		auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
		size_t n = 0;
		CURLcode rc = curl_easy_recv(c, buf_.data(), buf_.size(), &n);
		if (rc == CURLE_AGAIN) {
			if (remaining <= 0)
				return {Type::Timeout, {}};
			WaitSocket(false, static_cast<int>(std::min<long long>(remaining, 250)));
			continue;
		}
		if (rc != CURLE_OK || n == 0) {
			closed_ = true;
			close_reason_ = n == 0 && rc == CURLE_OK ? "connection closed by server" : curl_easy_strerror(rc);
			return {rc == CURLE_OK ? Type::Closed : Type::Error, close_reason_};
		}
		rx_.append(buf_.data(), n);
	}
}

} // namespace tandem::chat
