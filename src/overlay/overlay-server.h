// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - optional local chat overlay for an OBS Browser Source.
//
// A small dependency-free HTTP server bound to 127.0.0.1 only. It serves one self-contained
// HTML page and a live event stream built from a ChatHub subscription.
//
// Live transport is Server-Sent Events (GET /events, text/event-stream) instead of WebSocket:
// OBS Browser Source (CEF) supports EventSource natively, including automatic reconnects,
// SSE needs no handshake (no SHA-1/base64 upgrade, no frame masking), and the data only flows
// one way (server to page), which is exactly what an overlay needs.
//
// Routes (GET or HEAD only): / and /overlay (page), /events (SSE), /health ({"ok":true}).
// SSE event names: message, delete, clearuser, clearall, status.
#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace tandem::chat {
class ChatHub;
}

namespace tandem::overlay {

struct OverlayOptions {
	int port = 48080;             // 1-65535; 0 picks an ephemeral port (tests)
	size_t history_count = 50;    // max history messages replayed to a new /events client (?max= can lower it)
	int heartbeat_ms = 15000;     // SSE comment heartbeat interval
	int header_timeout_ms = 5000; // time allowed to receive the complete request head
	int max_clients = 16;         // concurrent connections; extras get 503
};

class OverlayServer {
public:
	OverlayServer();
	~OverlayServer();
	OverlayServer(const OverlayServer &) = delete;
	OverlayServer &operator=(const OverlayServer &) = delete;

	// Binds 127.0.0.1:<opt.port> and starts serving on background threads. Returns false with
	// a readable reason in *err (if non-null) when the port is in use or the socket cannot be
	// set up. Never falls back to a different port. Fails if already running.
	bool Start(chat::ChatHub *hub, const OverlayOptions &opt, std::string *err);

	// Closes the listener and every client and joins all threads (bounded, well under 2 s).
	// Safe to call when not running.
	void Stop();

	bool running() const;
	int port() const;

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace tandem::overlay
