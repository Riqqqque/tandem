#include "output-config.h"
#include "pch.h"

#include <obs.h>
#include <obs-frontend-api.h>
#include <random>
#include <filesystem>
#include <unordered_set>
#include <algorithm>
#include <optional>
#include <util/platform.h>
#include "json-util.hpp"
#include "secrets.h"
#include "platforms.h"


MultiOutputConfig& GlobalMultiOutputConfig()
{
    static MultiOutputConfig instance;
    return instance;
}

static uint64_t g_configRevision = 0;

uint64_t MultiOutputConfigRevision()
{
    return g_configRevision;
}


static nlohmann::json SaveTarget(OutputTargetConfig& config) {
    nlohmann::json json;
    json["id"] = config.id;
    json["name"] = config.name;
    json["protocol"] = config.protocol;
    json["platform"] = config.platform;
    json["bandwidth-test"] = config.bandwidthTest;
    json["service-param"] = config.serviceParam;
    if (tandem::SecretStoreAvailable() && json["service-param"].is_object()) {
        auto& service = json["service-param"];
        for (auto field : kSecretServiceFields) {
            auto it = service.find(field);
            if (it == service.end() || !it->is_string())
                continue;
            auto name = "target/" + config.id + "/" + field;
            if (tandem::SecretSet(name, it->get<std::string>()))
                service.erase(it);
            else
                blog(LOG_WARNING, TAG "Could not store a credential for target %s in the credential store; keeping it in the profile config.", config.id.c_str());
        }
    }
    json["output-param"] = config.outputParam;
    json["sync-start"] = config.syncStart;
    json["sync-stop"] = config.syncStop;
    if (config.videoConfig.has_value())
        json["video-config"] = *config.videoConfig;
    if (config.audioConfig.has_value())
        json["audio-config"] = *config.audioConfig;
    return json;
}

static nlohmann::json SaveVideoConfig(VideoEncoderConfig& config) {
    nlohmann::json json;
    json["id"] = config.id;
    json["encoder"] = config.encoderId;
    json["param"] = config.encoderParams;
    if (config.outputScene.has_value())
        json["scene"] = *config.outputScene;
    if (config.resolution.has_value())
        json["resolution"] = *config.resolution;
    json["fps-denumerator"] = config.fpsDenumerator;
    return json;
}

static nlohmann::json SaveAudioTrackConfig(AudioTrackConfig &config) {
	nlohmann::json json;
	json["mixer_track"] = config.mixer_track;
	json["output_track"] = config.output_track;
	return json;
}

static nlohmann::json SaveAudioConfig(AudioEncoderConfig& config) {
    nlohmann::json json;
    json["id"] = config.id;
    json["encoder"] = config.encoderId;
    json["param"] = config.encoderParams;
    json["mixerId"] = config.mixerId;


    nlohmann::json audio_tracks(nlohmann::json::value_t::array);
    for(auto& track: config.audioTracks) {
        audio_tracks.push_back(SaveAudioTrackConfig(*track));
    }

    json["audioTracks"] = audio_tracks;

    return json;
}

static nlohmann::json SaveChatPlatform(const ChatPlatformConfig& p) {
    nlohmann::json json;
    json["enabled"] = p.enabled;
    json["visible"] = p.visible;
    json["channel"] = p.channel;
    json["extra"] = p.extra;
    return json;
}

static nlohmann::json SaveChatConfig(const ChatConfig& c) {
    nlohmann::json json;
    json["twitch"] = SaveChatPlatform(c.twitch);
    json["kick"] = SaveChatPlatform(c.kick);
    json["youtube"] = SaveChatPlatform(c.youtube);
    if (tandem::SecretStoreAvailable()) {
        if (!tandem::SecretSet("chat/youtube/api-key", c.youtubeApiKey))
            json["youtube-api-key"] = c.youtubeApiKey;
    } else {
        json["youtube-api-key"] = c.youtubeApiKey;
    }
    json["youtube-quota-day"] = c.youtubeQuotaDay;
    json["youtube-quota-used"] = c.youtubeQuotaUsed;
    json["start-with-stream"] = c.startWithStream;
    json["show-timestamps"] = c.showTimestamps;
    json["third-party-emotes"] = c.thirdPartyEmotes;
    json["max-messages"] = c.maxMessages;
    json["overlay-enabled"] = c.overlayEnabled;
    json["overlay-port"] = c.overlayPort;
    return json;
}

static ChatPlatformConfig LoadChatPlatform(nlohmann::json& json, const char* key) {
    ChatPlatformConfig p;
    auto obj = GetJsonField<nlohmann::json>(json, key);
    if (!obj)
        return p;
    p.enabled = GetJsonField<bool>(*obj, "enabled").value_or(false);
    p.visible = GetJsonField<bool>(*obj, "visible").value_or(true);
    p.channel = GetJsonField<std::string>(*obj, "channel").value_or("");
    p.extra = GetJsonField<std::string>(*obj, "extra").value_or("");
    return p;
}

static ChatConfig LoadChatConfig(nlohmann::json& root) {
    ChatConfig c;
    auto obj = GetJsonField<nlohmann::json>(root, "chat");
    if (!obj)
        return c;
    auto& json = *obj;
    c.twitch = LoadChatPlatform(json, "twitch");
    c.kick = LoadChatPlatform(json, "kick");
    c.youtube = LoadChatPlatform(json, "youtube");
    c.youtubeApiKey = GetJsonField<std::string>(json, "youtube-api-key").value_or("");
    if (c.youtubeApiKey.empty() && tandem::SecretStoreAvailable())
        c.youtubeApiKey = tandem::SecretGet("chat/youtube/api-key").value_or("");
    c.youtubeQuotaDay = GetJsonField<std::string>(json, "youtube-quota-day").value_or("");
    c.youtubeQuotaUsed = GetJsonField<int64_t>(json, "youtube-quota-used").value_or(0);
    c.startWithStream = GetJsonField<bool>(json, "start-with-stream").value_or(true);
    c.showTimestamps = GetJsonField<bool>(json, "show-timestamps").value_or(false);
    c.thirdPartyEmotes = GetJsonField<bool>(json, "third-party-emotes").value_or(true);
    c.maxMessages = std::clamp(GetJsonField<int>(json, "max-messages").value_or(500), 50, 5000);
    c.overlayEnabled = GetJsonField<bool>(json, "overlay-enabled").value_or(false);
    c.overlayPort = std::clamp(GetJsonField<int>(json, "overlay-port").value_or(48080), 1024, 65535);
    return c;
}

static std::string SaveMultiOutputConfig(MultiOutputConfig& config) {
    nlohmann::json json;

    std::unordered_set<std::string> videoconfig_in_use;
    std::unordered_set<std::string> audioconfig_in_use;

    int target_count = 0, videocfg_count = 0, audiocfg_count = 0;

    nlohmann::json targets(nlohmann::json::value_t::array);
    for(auto& target: config.targets) {
        targets.push_back(SaveTarget(*target));
        if (target->videoConfig.has_value())
            videoconfig_in_use.insert(*target->videoConfig);
        if (target->audioConfig.has_value())
            audioconfig_in_use.insert(*target->audioConfig);
        ++target_count;
    }

    nlohmann::json video_configs(nlohmann::json::value_t::array);
    for(auto& video_config: config.videoConfig) {
        if (videoconfig_in_use.find(video_config->id) != videoconfig_in_use.end())
            video_configs.push_back(SaveVideoConfig(*video_config));
        ++videocfg_count;
    }

    nlohmann::json audio_configs(nlohmann::json::value_t::array);
    for(auto& audio_config: config.audioConfig) {
        if (audioconfig_in_use.find(audio_config->id) != audioconfig_in_use.end())
            audio_configs.push_back(SaveAudioConfig(*audio_config));
        ++audiocfg_count;
    }

    json["targets"] = targets;
    json["video_configs"] = video_configs;
    json["audio_configs"] = audio_configs;
    json["chat"] = SaveChatConfig(config.chat);
    json["upload-capacity-mbps"] = config.uploadCapacityMbps;
    json["ui-mode"] = config.uiMode;
    json["simple-quality"] = config.simpleQuality;

    blog(LOG_INFO, TAG "Save %d targets, %d video configs, %d audio configs", target_count, (int)video_configs.size(), (int)audio_configs.size());

    return json.dump();
}




static OutputTargetConfigPtr LoadTargetConfig(nlohmann::json& json) {
    auto id = GetJsonField<std::string>(json, "id");
    if (!id.has_value())
        return {};

    auto config = std::make_shared<OutputTargetConfig>();
    config->id = *id;
    config->name = GetJsonField<std::string>(json, "name").value_or("");
    config->protocol = GetJsonField<std::string>(json, "protocol").value_or("RTMP"); // for compatibility
    config->platform = GetJsonField<std::string>(json, "platform").value_or("");
    config->bandwidthTest = GetJsonField<bool>(json, "bandwidth-test").value_or(false);
    config->syncStart = GetJsonField<bool>(json, "sync-start").value_or(false);
    config->syncStop = GetJsonField<bool>(json, "sync-stop").value_or(config->syncStart);
    config->serviceParam = GetJsonField<nlohmann::json>(json, "service-param").value_or(nlohmann::json::object());
    if (tandem::SecretStoreAvailable()) {
        for (auto field : kSecretServiceFields) {
            if (config->serviceParam.contains(field))
                continue; // plain text from an older config; moved to the store on next save
            if (auto secret = tandem::SecretGet("target/" + config->id + "/" + field))
                config->serviceParam[field] = *secret;
        }
    }
    if (config->platform.empty())
        config->platform = tandem::GuessPlatformFromServer(config->serviceParam.value("server", std::string()));
    config->outputParam = GetJsonField<nlohmann::json>(json, "output-param").value_or(nlohmann::json{});
    config->videoConfig = GetJsonField<std::string>(json, "video-config");
    config->audioConfig = GetJsonField<std::string>(json, "audio-config");

    return config;
}

static VideoEncoderConfigPtr LoadVideoConfig(nlohmann::json& json) {
    auto id = GetJsonField<std::string>(json, "id");
    if (!id.has_value())
        return {};

    auto config = std::make_shared<VideoEncoderConfig>();
    config->id = *id;
    config->encoderId = GetJsonField<std::string>(json, "encoder").value_or("");
    config->outputScene = GetJsonField<std::string>(json, "scene");
    config->resolution = GetJsonField<std::string>(json, "resolution");
    config->fpsDenumerator = GetJsonField<int>(json, "fps-denumerator").value_or(1);
    config->encoderParams = GetJsonField<nlohmann::json>(json, "param").value_or(nlohmann::json{});

    return config;
}

static AudioTrackConfigPtr LoadAudioTrackConfig(nlohmann::json& json) {
    auto config = std::make_shared<AudioTrackConfig>();
    config->mixer_track = GetJsonField<int>(json, "mixer_track").value_or(0);
    config->output_track = GetJsonField<int>(json, "output_track").value_or(0);

    return config;
}

static AudioEncoderConfigPtr LoadAudioConfig(nlohmann::json& json) {
    auto id = GetJsonField<std::string>(json, "id");
    if (!id.has_value())
        return {};
    
    auto config = std::make_shared<AudioEncoderConfig>();
    config->id = *id;
    config->encoderId = GetJsonField<std::string>(json, "encoder").value_or("");
    config->mixerId = GetJsonField<int>(json, "mixerId").value_or(0);
    config->encoderParams = GetJsonField<nlohmann::json>(json, "param").value_or(nlohmann::json{});

    auto it = json.find("audioTracks");
    if (it != json.end() && it->type() == nlohmann::json::value_t::array) {
        for(auto& audio_track_json: *it) {
            if (audio_track_json.type() != nlohmann::json::value_t::object)
                continue;
            auto audio_track = LoadAudioTrackConfig(audio_track_json);
            if (audio_track)
                config->audioTracks.emplace_back(audio_track);
        }
    }

    return config;
}

static std::optional<MultiOutputConfig> LoadMultiOutputConfig(const std::string& content) {
    try {
        int target_count = 0, videocfg_count = 0, audiocfg_count = 0;

        auto json = nlohmann::json::parse(content);
        MultiOutputConfig config;
        auto it = json.find("targets");
        if (it != json.end() && it->type() == nlohmann::json::value_t::array) {
            for(auto& target_json: *it) {
                if (target_json.type() != nlohmann::json::value_t::object)
                    continue;
                auto target = LoadTargetConfig(target_json);
                if (target)
                    config.targets.emplace_back(target);
                ++target_count;
            }
        }

        it = json.find("video_configs");
        if (it != json.end() && it->type() == nlohmann::json::value_t::array) {
            for(auto& video_enc_json: *it) {
                if (video_enc_json.type() != nlohmann::json::value_t::object)
                    continue;
                auto video_enc = LoadVideoConfig(video_enc_json);
                if (video_enc) {
                    config.videoConfig.emplace_back(video_enc);
                }
                ++videocfg_count;
            }
        }

        it = json.find("audio_configs");
        if (it != json.end() && it->type() == nlohmann::json::value_t::array) {
            for(auto& audio_enc_json: *it) {
                if (audio_enc_json.type() != nlohmann::json::value_t::object)
                    continue;
                auto audio_enc = LoadAudioConfig(audio_enc_json);
                if (audio_enc) {
                    config.audioConfig.emplace_back(audio_enc);
                }
                ++audiocfg_count;
            }
        }

        config.chat = LoadChatConfig(json);
        config.uploadCapacityMbps = (std::max)(0.0, GetJsonField<double>(json, "upload-capacity-mbps").value_or(0.0));
        config.uiMode = GetJsonField<std::string>(json, "ui-mode").value_or("");
        config.simpleQuality = std::clamp(GetJsonField<int>(json, "simple-quality").value_or(0), 0, 2);

        blog(LOG_INFO, TAG "Load %d targets, %d video configs, %d audio configs", target_count, videocfg_count, audiocfg_count);

        return config;
    }
    catch(const nlohmann::json::parse_error& e) {
        // Only the position: the parser's message quotes file content, which can include keys.
        blog(LOG_ERROR, TAG "Config file is not valid JSON (error at byte %zu)", (size_t)e.byte);
        return std::nullopt;
    }
    catch(const std::exception&) {
        blog(LOG_ERROR, TAG "Config file has an unexpected structure");
        return std::nullopt;
    }
}

static const char* kConfigFile = "/tandem.json";
static const char* kLegacyConfigFile = "/obs-multi-rtmp.json";

void SaveMultiOutputConfig() {
    auto profiledir = obs_frontend_get_current_profile_path();
    if (profiledir) {
        std::string filename = profiledir;
        filename += kConfigFile;
        auto content = SaveMultiOutputConfig(GlobalMultiOutputConfig());
        ++g_configRevision;
        if (os_quick_write_utf8_file_safe(filename.c_str(), content.c_str(), content.size(), false, "tmp", "bak"))
            blog(LOG_INFO, TAG "Saved config into %s", filename.c_str());
        else
            blog(LOG_ERROR, TAG "Failed to save config into %s", filename.c_str());
    }
    bfree(profiledir);
}


static bool HasPlainTextSecrets(const MultiOutputConfig& config, const std::string& content) {
    if (content.find("\"youtube-api-key\"") != std::string::npos && !config.chat.youtubeApiKey.empty())
        return true;
    try {
        auto json = nlohmann::json::parse(content);
        auto targets = json.find("targets");
        if (targets == json.end() || !targets->is_array())
            return false;
        for (auto& target : *targets) {
            auto service = target.find("service-param");
            if (service == target.end() || !service->is_object())
                continue;
            for (auto field : kSecretServiceFields) {
                auto it = service->find(field);
                if (it != service->end() && it->is_string() && !it->get<std::string>().empty())
                    return true;
            }
        }
    } catch (...) {
    }
    return false;
}

bool LoadMultiOutputConfig() {
    auto profiledir = obs_frontend_get_current_profile_path();
    bool ret = false;
    if (profiledir) {
        std::string filename = std::string(profiledir) + kConfigFile;
        bool legacy = false;
        auto content = os_quick_read_utf8_file(filename.c_str());
        if (!content) {
            // First run: import targets from obs-multi-rtmp; its file is left untouched.
            content = os_quick_read_utf8_file((std::string(profiledir) + kLegacyConfigFile).c_str());
            legacy = content != nullptr;
        }
        if (content) {
            std::string text = content;
            bfree(content);
            auto loaded = LoadMultiOutputConfig(text);
            ++g_configRevision;
            if (!loaded) {
                // Keep the damaged file for the user instead of overwriting it on the next save.
                GlobalMultiOutputConfig() = {};
                if (!legacy) {
                    std::string broken = filename + ".broken";
                    os_unlink(broken.c_str());
                    if (os_rename(filename.c_str(), broken.c_str()) == 0)
                        blog(LOG_WARNING, TAG "Moved the unreadable config to %s; starting with an empty one", broken.c_str());
                }
                bfree(profiledir);
                return false;
            }
            GlobalMultiOutputConfig() = std::move(*loaded);
            ret = true;
            blog(LOG_INFO, TAG "Loaded config from %s", legacy ? "obs-multi-rtmp.json (import)" : filename.c_str());

            if (legacy || (tandem::SecretStoreAvailable() && HasPlainTextSecrets(GlobalMultiOutputConfig(), text))) {
                // Write tandem.json right away so credentials move to the credential store.
                SaveMultiOutputConfig();
                if (!legacy)
                    os_unlink((filename + ".bak").c_str());
            }
        } else {
            ++g_configRevision;
            blog(LOG_INFO, TAG "No config at %s yet", filename.c_str());
        }
    }
    bfree(profiledir);
    return ret;
}

void RemoveTargetSecrets(const std::string& targetId) {
    for (auto field : kSecretServiceFields)
        tandem::SecretRemove("target/" + targetId + "/" + field);
}


template<class T>
static bool has_id(T& container, const std::string& id) {
    for(auto& item: container) {
        if (item->id == id)
            return true;
    }
    return false;
}

std::string GenerateId(MultiOutputConfig& config) {
    static std::random_device rndgen;
    for(;;) {
        auto rndnum = rndgen();
        auto newid = std::to_string(rndnum);
        if (has_id(config.targets, newid))
            continue;
        if (has_id(config.audioConfig, newid))
            continue;
        if (has_id(config.videoConfig, newid))
            continue;
        return newid;
    }
}
