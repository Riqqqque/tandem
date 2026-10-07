// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - automatic encoder choice for Simple mode.
#pragma once

#include <string>

#include <json.hpp>

namespace tandem {

enum class Quality { Auto = 0, Lower = 1, Higher = 2 };

struct AutoEncoderChoice {
	std::string encoderId;   // empty when nothing usable was found
	std::string label;       // e.g. "NVIDIA NVENC H.264"
	int kbps = 0;
	nlohmann::json settings; // encoder settings to store in the video config
};

// Best available H.264 encoder: NVIDIA, AMD, Intel or Apple hardware first, x264 as the
// fallback. H.264 is what every platform's RTMP ingest accepts. Bitrate follows OBS's output
// resolution and frame rate, kept within what Twitch accepts.
AutoEncoderChoice PickAutoEncoder(Quality quality);

// AAC audio settings used by Simple mode (160 kbps, mixer track 1).
const char *AutoAudioEncoderId();
nlohmann::json AutoAudioSettings();

} // namespace tandem
