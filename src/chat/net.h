// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - thin RAII wrappers over libcurl (HTTP GET, server-streaming GET, WebSocket).
#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::chat {

// Each entry is a full header line, "Name: value". A "User-Agent: ..." entry overrides the default.
using HttpHeaders = std::vector<std::string>;

struct HttpResponse {
	long status = 0;         // HTTP status, 0 when no response was received
	std::string body;        // response body (for HttpStream: only the body of a >= 400 response)
	std::string error;       // transport error text, empty on success; never contains the URL
	long retry_after_s = 0;  // parsed Retry-After header, 0 when absent
	bool cancelled = false;  // the cancel flag was raised or the chunk callback asked to stop
	bool idle_timeout = false; // HttpStream only: no data for idle_timeout_ms
};

// One-time, thread-safe curl_global_init. Safe to call when the host already initialized curl.
void EnsureCurlInit();

// True when the loaded libcurl lists "wss" among its protocols.
bool CurlSupportsWss();

// True when secure WebSockets are available at all: through libcurl's own WebSocket API or
// through Tandem's fallback over a plain libcurl TLS connection (any libcurl with TLS).
bool WebSocketsAvailable();

// "Tandem/<version>"
const char *DefaultUserAgent();

// Blocking GET. Aborts promptly when *cancel becomes true. TLS peers are always verified.
// follow_redirects should be false for requests carrying secrets in custom headers, because
// libcurl re-sends custom headers to the redirect target.
HttpResponse HttpGet(const std::string &url, const HttpHeaders &headers, long timeout_ms,
		     const std::atomic<bool> *cancel, bool follow_redirects = true);

// Long-lived GET for server-streaming responses. on_chunk receives body bytes of a 2xx response
// as they arrive and returns false to stop. The transfer ends on completion, error, cancel, or
// when no bytes arrive for idle_timeout_ms.
using ChunkFn = std::function<bool(std::string_view)>;
HttpResponse HttpStream(const std::string &url, const HttpHeaders &headers, const ChunkFn &on_chunk,
			const std::atomic<bool> *cancel, long idle_timeout_ms, bool follow_redirects = true);

// Client WebSocket over libcurl's CONNECT_ONLY mode. Not thread-safe: use it from one thread.
// PING frames are answered by libcurl itself (auto-pong) and never surface from Recv().
class WebSocket {
public:
	struct RecvResult {
		enum class Type { Text, Closed, Timeout, Error };
		Type type = Type::Timeout;
		std::string data; // Text: the message; Closed/Error: a short reason
	};

	WebSocket();
	~WebSocket();
	WebSocket(const WebSocket &) = delete;
	WebSocket &operator=(const WebSocket &) = delete;

	// Optional flag that aborts Connect/Recv/SendText promptly.
	void SetCancelFlag(const std::atomic<bool> *cancel) { cancel_ = cancel; }

	bool Connect(const std::string &url, const HttpHeaders &headers, long timeout_ms, std::string *err);
	bool SendText(std::string_view text);
	// Waits up to timeout_ms for one complete message. Fragmented and multi-chunk frames are
	// reassembled; a partially received message is kept for the next call.
	RecvResult Recv(int timeout_ms);
	// Sends a close frame (best effort) and releases the connection.
	void Close();
	bool IsOpen() const { return curl_ != nullptr; }

	static constexpr size_t kMaxMessageBytes = 8 * 1024 * 1024;

private:
	bool Cancelled() const { return cancel_ && cancel_->load(); }
	bool WaitSocket(bool for_write, int timeout_ms);
	void Release();

	// Fallback for libcurl builds without WebSocket support (e.g. the macOS system curl):
	// libcurl only provides the TLS connection and Tandem does the RFC 6455 handshake and
	// framing itself.
	bool ConnectRaw(const std::string &url, const HttpHeaders &headers, long timeout_ms, std::string *err);
	bool RawSendAll(const void *data, size_t len, int timeout_ms);
	bool RawSendFrame(int opcode, const void *data, size_t len);
	RecvResult RawRecv(int timeout_ms);
	bool raw_ = false;
	std::string rx_; // raw mode: bytes received but not yet parsed into frames

	void *curl_ = nullptr; // CURL*
	void *headers_ = nullptr; // curl_slist*
	const std::atomic<bool> *cancel_ = nullptr;

	std::string message_;    // message being assembled
	bool frame_done_ = true; // the last data frame of message_ was fully received
	bool control_frame_ = false; // currently reading a control frame (close/ping/pong)
	bool close_frame_ = false;
	std::string close_payload_;
	std::deque<std::string> ready_; // completed messages not yet returned
	std::vector<char> buf_;
	bool closed_ = false;
	std::string close_reason_;
};

} // namespace tandem::chat
