#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <memory>
#include <list>

#include <json.hpp>

struct VideoEncoderConfig {
    std::string id;
    std::string encoderId;
    int fpsDenumerator = 1;
    nlohmann::json encoderParams;
    std::optional<std::string> outputScene;
    std::optional<std::string> resolution;
};
using VideoEncoderConfigPtr = std::shared_ptr<VideoEncoderConfig>;

struct AudioTrackConfig {
    int mixer_track;
    int output_track;
};
using AudioTrackConfigPtr = std::shared_ptr<AudioTrackConfig>;

struct AudioEncoderConfig {
    std::string id;
    std::string encoderId;
    nlohmann::json encoderParams;
    int mixerId = 0;
    std::list<AudioTrackConfigPtr> audioTracks;
};
using AudioEncoderConfigPtr = std::shared_ptr<AudioEncoderConfig>; 


struct OutputTargetConfig {
    std::string id;
    std::string name;
    std::string protocol = "RTMP";
    // "twitch", "kick", "youtube" or "custom"; only used for presets and labels.
    std::string platform = "custom";
    // Opt-in: start/stop together with the OBS stream and with "Start enabled".
    bool syncStart = false;
    bool syncStop = false;
    // Twitch only: append ?bandwidthtest=true to the key so the stream is not shown to viewers.
    bool bandwidthTest = false;

    nlohmann::json serviceParam;
    nlohmann::json outputParam;

    std::optional<std::string> videoConfig;
    std::optional<std::string> audioConfig;
};
using OutputTargetConfigPtr = std::shared_ptr<OutputTargetConfig>;


struct ChatPlatformConfig {
    bool enabled = false;   // read chat from this platform
    bool visible = true;    // dock filter
    std::string channel;    // Twitch login / Kick slug / YouTube video id or URL
    std::string extra;      // Kick: manual chatroom id; YouTube: channel id
};

struct ChatConfig {
    ChatPlatformConfig twitch;
    ChatPlatformConfig kick;
    ChatPlatformConfig youtube;
    std::string youtubeApiKey;      // kept in the OS credential store when available
    std::string youtubeQuotaDay;    // YYYY-MM-DD (Pacific time, when Google resets quota)
    int64_t youtubeQuotaUsed = 0;
    bool startWithStream = true;
    bool showTimestamps = false;
    int maxMessages = 500;
    bool overlayEnabled = false;
    int overlayPort = 48080;
};

struct MultiOutputConfig {
public:
    std::list<OutputTargetConfigPtr> targets;
    std::list<VideoEncoderConfigPtr> videoConfig;
    std::list<AudioEncoderConfigPtr> audioConfig;
    ChatConfig chat;
    // Optional upload capacity in Mbps used for the bandwidth warning; 0 = unknown.
    double uploadCapacityMbps = 0;
    // Dock mode: "simple" or "advanced". Empty until the user picks one.
    std::string uiMode;
    // Simple mode quality: 0 = automatic, 1 = lower bitrate, 2 = higher bitrate.
    int simpleQuality = 0;
};

// Service settings that hold credentials and never go into the JSON file when the
// OS credential store is available.
inline const char *const kSecretServiceFields[] = { "key", "password", "bearer_token" };

void RemoveTargetSecrets(const std::string &targetId);

template<class T, class S>
inline T FindById(std::list<T>& list, const S& id) {
    for(auto& x: list) {
        if (x->id == id)
            return x;
    }
    return nullptr;
}


MultiOutputConfig& GlobalMultiOutputConfig();

void SaveMultiOutputConfig();

bool LoadMultiOutputConfig();

std::string GenerateId(MultiOutputConfig& config);
