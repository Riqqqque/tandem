// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - owns the chat providers, the hub and the overlay server. UI thread only.
#pragma once

#include <memory>
#include <string>

#include <obs-frontend-api.h>

#include "chat/chat-hub.h"
#include "chat/emotes.h"
#include "output-config.h"

namespace tandem::overlay {
class OverlayServer;
}

class ChatController {
public:
	ChatController();
	~ChatController();

	tandem::chat::ChatHub &Hub() { return hub_; }

	// Re-reads GlobalMultiOutputConfig().chat. Running providers whose settings
	// changed are restarted; the overlay follows its own toggle.
	void ApplyConfig();

	void StartChat();
	void StopChat();
	bool IsRunning() const { return running_; }

	void OnFrontendEvent(obs_frontend_event event);
	void Shutdown();

	// Overlay state for the settings dialog.
	bool OverlayRunning() const;
	std::string OverlayError() const { return overlayError_; }
	std::string OverlayUrl() const;

private:
	void StartProvider(int index);
	void StopProvider(int index);
	void UpdateOverlay();
	void OnQuota(int64_t used);

	tandem::chat::ChatHub hub_;
	// Between the providers and the hub: adds 7TV/BTTV/FFZ emotes. Declared after hub_ so it is
	// destroyed first.
	tandem::chat::EmoteAnnotator emotes_{&hub_};
	std::unique_ptr<tandem::chat::ChatProvider> providers_[tandem::chat::kPlatformCount];
	ChatPlatformConfig applied_[tandem::chat::kPlatformCount];
	std::string appliedYouTubeKey_;
	bool running_ = false;
	bool startedWithStream_ = false;
	bool shutdown_ = false;
	std::unique_ptr<tandem::overlay::OverlayServer> overlay_;
	int overlayPort_ = 0;
	std::string overlayError_;
};

ChatController &GetChatController();

// Current date in Pacific time (YouTube's quota day), formatted YYYY-MM-DD.
std::string YouTubeQuotaDay();
