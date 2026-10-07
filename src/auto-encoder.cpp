// SPDX-License-Identifier: GPL-2.0-or-later
#include "auto-encoder.h"

#include <obs.h>

#include <algorithm>
#include <cstring>

namespace tandem {

namespace {

struct Candidate {
	const char *id;
	const char *label;
	const char *rateControl; // value for "rate_control"
};

// Preference order. Hardware encoders leave the CPU free for games; x264 always works.
const Candidate kCandidates[] = {
	{"obs_nvenc_h264_tex", "NVIDIA NVENC H.264", "cbr"},
	{"jim_nvenc", "NVIDIA NVENC H.264", "CBR"},
	{"h264_texture_amf", "AMD AMF H.264", "CBR"},
	{"obs_qsv11_v2", "Intel QuickSync H.264", "CBR"},
	{"com.apple.videotoolbox.videoencoder.ave.avc", "Apple VideoToolbox H.264", "CBR"},
	{"obs_x264", "x264 (CPU) H.264", "CBR"},
};

bool EncoderAvailable(const char *id)
{
	const char *type = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &type); ++i) {
		if (type && strcmp(type, id) == 0) {
			uint32_t caps = obs_get_encoder_caps(id);
			const char *codec = obs_get_encoder_codec(id);
			return !(caps & OBS_ENCODER_CAP_DEPRECATED) && codec && strcmp(codec, "h264") == 0;
		}
	}
	return false;
}

int RecommendedKbps()
{
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi) || ovi.fps_den == 0)
		return 4500;
	double fps = (double)ovi.fps_num / ovi.fps_den;
	uint32_t h = ovi.output_height;
	if (h >= 1080)
		return fps >= 50 ? 6000 : 4500;
	if (h >= 720)
		return fps >= 50 ? 4500 : 3000;
	return 2000;
}

} // namespace

AutoEncoderChoice PickAutoEncoder(Quality quality)
{
	AutoEncoderChoice choice;
	int kbps = RecommendedKbps();
	if (quality == Quality::Lower)
		kbps = kbps * 7 / 10;
	else if (quality == Quality::Higher)
		kbps = std::min(8000, kbps * 4 / 3);
	kbps = std::max(1000, kbps / 100 * 100);

	for (const auto &c : kCandidates) {
		if (!EncoderAvailable(c.id))
			continue;
		choice.encoderId = c.id;
		choice.label = c.label;
		choice.kbps = kbps;
		choice.settings = {
			{"rate_control", c.rateControl},
			{"bitrate", kbps},
			{"keyint_sec", 2},
			{"profile", "high"},
		};
		break;
	}
	return choice;
}

const char *AutoAudioEncoderId()
{
	return "ffmpeg_aac";
}

nlohmann::json AutoAudioSettings()
{
	return {{"bitrate", 160}};
}

} // namespace tandem
