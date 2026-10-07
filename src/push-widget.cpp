#include "pch.h"
#include "helpers.h"
#include <cmath>
#include <deque>
#include <regex>
#include <optional>
#include <tuple>
#include "push-widget.h"
#include "edit-widget.h"
#include "output-config.h"
#include "protocols.h"
#include "platforms.h"

#include "obs.hpp"
#include <util/platform.h>

class IOBSOutputEventHanlder
{
public:
    virtual void OnStarting() {}
    static void OnOutputStarting(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnStarting();
    }

    virtual void OnStarted() {}
    static void OnOutputStarted(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnStarted();
    }

    virtual void OnStopping() {}
    static void OnOutputStopping(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnStopping();
    }

    virtual void OnStopped(int, std::string) {}
    static void OnOutputStopped(void* x, calldata_t* param)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        const char* lastError = calldata_string(param, "last_error");
        thiz->OnStopped((int)calldata_int(param, "code"), lastError ? lastError : "");
    }

    virtual void OnReconnect() {}
    static void OnOutputReconnect(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnReconnect();
    }

    virtual void OnReconnected() {}
    static void OnOutputReconnected(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnReconnected();
    }

    virtual void onDeactive() {}
    static void OnOutputDeactive(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->onDeactive();
    }

    void SetMeAsHandler(obs_output_t* output)
    {
        auto outputSignal = obs_output_get_signal_handler(output);
        if (outputSignal)
        {
            signal_handler_connect(outputSignal, "starting", &IOBSOutputEventHanlder::OnOutputStarting, this);
            signal_handler_connect(outputSignal, "start", &IOBSOutputEventHanlder::OnOutputStarted, this);
            signal_handler_connect(outputSignal, "reconnect", &IOBSOutputEventHanlder::OnOutputReconnect, this);
            signal_handler_connect(outputSignal, "reconnect_success", &IOBSOutputEventHanlder::OnOutputReconnected, this);
            signal_handler_connect(outputSignal, "stopping", &IOBSOutputEventHanlder::OnOutputStopping, this);
            signal_handler_connect(outputSignal, "deactivate", &IOBSOutputEventHanlder::OnOutputDeactive, this);
            signal_handler_connect(outputSignal, "stop", &IOBSOutputEventHanlder::OnOutputStopped, this);
        }
    }

    void DisconnectSignals(obs_output_t* output)
    {
        auto outputSignal = obs_output_get_signal_handler(output);
        if (outputSignal)
        {
            signal_handler_disconnect(outputSignal, "starting", &IOBSOutputEventHanlder::OnOutputStarting, this);
            signal_handler_disconnect(outputSignal, "start", &IOBSOutputEventHanlder::OnOutputStarted, this);
            signal_handler_disconnect(outputSignal, "reconnect", &IOBSOutputEventHanlder::OnOutputReconnect, this);
            signal_handler_disconnect(outputSignal, "reconnect_success", &IOBSOutputEventHanlder::OnOutputReconnected, this);
            signal_handler_disconnect(outputSignal, "stopping", &IOBSOutputEventHanlder::OnOutputStopping, this);
            signal_handler_disconnect(outputSignal, "deactivate", &IOBSOutputEventHanlder::OnOutputDeactive, this);
            signal_handler_disconnect(outputSignal, "stop", &IOBSOutputEventHanlder::OnOutputStopped, this);
        }
    }
};


static double EncoderSettingsKbps(obs_encoder_t* enc) {
    if (!enc)
        return 0;
    OBSDataAutoRelease settings = obs_encoder_get_settings(enc);
    return (double)obs_data_get_int(settings, "bitrate");
}

double EstimateMainStreamKbps() {
    if (!IsFrontendReady())
        return 0;
    OBSOutputAutoRelease stream = obs_frontend_get_streaming_output();
    if (stream) {
        double kbps = EncoderSettingsKbps(obs_output_get_video_encoder(stream)) +
            EncoderSettingsKbps(obs_output_get_audio_encoder(stream, 0));
        if (kbps > 0)
            return kbps;
    }

    auto profile = obs_frontend_get_profile_config();
    if (!profile)
        return 0;
    const char* mode = config_get_string(profile, "Output", "Mode");
    if (mode && strcmp(mode, "Advanced") == 0) {
        double kbps = 0;
        if (char* dir = obs_frontend_get_current_profile_path()) {
            std::string path = std::string(dir) + "/streamEncoder.json";
            bfree(dir);
            OBSDataAutoRelease enc = obs_data_create_from_json_file_safe(path.c_str(), "bak");
            if (enc)
                kbps += (double)obs_data_get_int(enc, "bitrate");
        }
        int track = (int)config_get_int(profile, "AdvOut", "TrackIndex");
        if (track < 1 || track > 6)
            track = 1;
        kbps += (double)config_get_int(profile, "AdvOut", ("Track" + std::to_string(track) + "Bitrate").c_str());
        return kbps;
    }
    return (double)config_get_int(profile, "SimpleOutput", "VBitrate") +
        (double)config_get_int(profile, "SimpleOutput", "ABitrate");
}


class PushWidgetImpl : public PushWidget, public IOBSOutputEventHanlder
{
    std::string targetid_;
    OutputTargetConfigPtr config_;

    QCheckBox* enabled_ = 0;
    QPushButton* btn_ = 0;
    QLabel* name_ = 0;
    QLabel* msg_ = 0;

    using clock = std::chrono::steady_clock;
    clock::time_point begin_time_;
    clock::time_point last_info_time_;
    clock::time_point connected_time_;
    uint64_t total_frames_ = 0;
    uint64_t total_bytes_ = 0;
    double current_kbps_ = 0;
    QTimer* timer_ = 0;

    QPushButton* edit_btn_ = 0;
    QPushButton* remove_btn_ = 0;

    obs_output_t* output_ = 0;
    bool using_main_video_encoder_ = false;
    bool using_main_audio_encoder_ = false;
    obs_view_t* scene_view_ = 0;
    bool isUseDelay_ = false;
    // Set from obs_output_start until the output is active or stopped. libobs only
    // reports an output as active once it has connected, so without this a target
    // that is still connecting looks idle and Start/Stop would restart it.
    bool starting_ = false;
    bool reconnecting_ = false;
    // Generation of the current output; queued callbacks from an older output are ignored.
    uint64_t startGeneration_ = 0;
    // Reconnects that followed a connection lasting under kQuickDropSeconds.
    std::deque<clock::time_point> quickDrops_;
    bool stoppedForLoop_ = false;
    std::string codecWarning_;

    static constexpr int kQuickDropSeconds = 30;
    static constexpr int kQuickDropWindowSeconds = 300;
    static constexpr size_t kQuickDropLimit = 5;

    QPushButton* GetDeleteButton() override {
        return remove_btn_;
    }

    std::string GetTargetId() const override {
        return targetid_;
    }

    bool PrepareOutputService()
    {
        if (!output_) {
            blog(LOG_ERROR, TAG "Prepare output service before output object is created.");
            return false;
        }

        ReleaseOutputService();

        auto protocolInfo = GetProtocolInfos()->GetInfo(config_->protocol.c_str());
        if (!protocolInfo) {
        	blog(LOG_ERROR, TAG "Invalid protocol \"%s\", maybe broken config file.", config_->protocol.c_str());
        	return false;
        }
        auto service_id = protocolInfo->serviceId;

        auto serviceParam = config_->serviceParam;
        if (config_->platform == "twitch" && config_->bandwidthTest && serviceParam.is_object()) {
            // Twitch's bandwidth test mode: the stream reaches ingest but is not shown.
            auto key = serviceParam.value("key", std::string());
            if (!key.empty() && key.find("bandwidthtest=") == std::string::npos) {
                key += key.find('?') == std::string::npos ? "?bandwidthtest=true" : "&bandwidthtest=true";
                serviceParam["key"] = key;
            }
        }
        OBSDataAutoRelease conf = obs_data_create_from_json(serviceParam.dump().c_str());
        if (!conf)
            return false;

        auto service = obs_service_create(service_id, ("Tandem service: " + config_->name).c_str(), conf, nullptr);
        if (!service)
            return false;
        obs_output_set_service(output_, service);

        return true;
    }


    bool ReleaseOutputService()
    {
        if (!output_)
            return true;

        if (obs_output_active(output_)) {
            return false;
        }

        auto service = obs_output_get_service(output_);
        if (service)
        {
            obs_output_set_service(output_, nullptr);
            obs_service_release(service);
        }
        return true;
    }


    bool PrepareEncoderSource() {
        if (!output_) {
            blog(LOG_ERROR, TAG "Prepare output scene before output object is created.");
            return false;
        }

        if (!using_main_video_encoder_) {
            auto venc = obs_output_get_video_encoder(output_);
            if (!venc) {
                blog(LOG_ERROR, TAG "Prepare output scene before encoder is created.");
                return false;
            }
            if (!obs_encoder_active(venc)) {
                auto videoConfig = FindById(GlobalMultiOutputConfig().videoConfig, config_->videoConfig.value_or(""));

                if (!videoConfig || !videoConfig->outputScene.has_value()) {
                    obs_encoder_set_video(venc, obs_get_video());
                } else {
                    auto sceneName = *videoConfig->outputScene;
                    OBSSourceAutoRelease scene = obs_get_source_by_name(sceneName.c_str());
                    if (scene == nullptr) {
                        blog(LOG_ERROR, TAG "Output scene is not found.");
                        return false;
                    }
                    ReleaseOutputSceneView();

                    scene_view_ = obs_view_create();
                    obs_view_set_source(scene_view_, 0, scene);
                    obs_source_inc_active(scene);
                    auto scene_video = obs_view_add(scene_view_);
                    obs_encoder_set_video(venc, scene_video);
                }
            }
        }

        if (!using_main_audio_encoder_) {
            for (size_t idx = 0; idx < MAX_OUTPUT_AUDIO_ENCODERS; ++idx) {
                auto enc = obs_output_get_audio_encoder(output_, idx);
                if (enc && !obs_encoder_active(enc))
                    obs_encoder_set_audio(enc, obs_get_audio());
            }
            if (!obs_output_get_audio_encoder(output_, 0)) {
                blog(LOG_ERROR, TAG "Prepare output scene before encoder is created.");
                return false;
            }
        }

        return true;
    }


    bool ReleaseOutputSceneView() {
        if (!scene_view_)
            return true;

        obs_view_remove(scene_view_);
        OBSSourceAutoRelease source = obs_view_get_source(scene_view_, 0);
        if (source) {
            obs_source_dec_active(source);
        }
        obs_view_set_source(scene_view_, 0, nullptr);
        obs_view_destroy(scene_view_);
        scene_view_ = nullptr;

        return true;
    }


    // The encoder id is part of the name so that changing a shared config's encoder
    // type creates a fresh encoder instead of reusing the old one by name.
    std::string VideoEncoderName(const std::string& encoderId) {
        return "tandem-venc-" + config_->videoConfig.value_or("") + "-" + encoderId;
    }

    std::string AudioEncoderName(const std::string& encoderId, int track) {
        return "tandem-aenc-" + config_->audioConfig.value_or("") + "-" + encoderId + "-track-" + std::to_string(track);
    }

    std::optional<std::tuple<int, int>> ParseResolution(const std::optional<std::string>& res) {
        if (!res.has_value())
            return std::nullopt;
        std::regex res_pattern(R"__(\s*(\d{1,5})\s*x\s*(\d{1,5})\s*)__");
        std::smatch match;
        if (std::regex_match(*res, match, res_pattern))
        {
            auto width = std::stoi(match[1].str());
            auto height = std::stoi(match[2].str());
            if (width > 0 && height > 0)
                return {{ width, height }};
        }

        return std::nullopt;
    }

    obs_service_t* CurrentService() {
        return output_ ? obs_output_get_service(output_) : nullptr;
    }

    OBSEncoder GetVideoEncoder() {
        auto config_id = config_->videoConfig.value_or(OBS_STREAMING_ENC_PLACEHOLDER);
        if (config_id == "" || config_id == OBS_STREAMING_ENC_PLACEHOLDER) {
            OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();
            OBSEncoder enc = obs_output_get_video_encoder(stream_output);
            using_main_video_encoder_ = true;
            return enc.Get();
        } else if (config_id == OBS_RECORDING_ENC_PLACEHOLDER) {
            OBSOutputAutoRelease stream_output = obs_frontend_get_recording_output();
            OBSEncoder enc = obs_output_get_video_encoder(stream_output);
            using_main_video_encoder_ = true;
            return enc.Get();
        }

        auto& global = GlobalMultiOutputConfig();
        auto videoConfig = FindById(global.videoConfig, config_id);
        if (!videoConfig) {
            blog(LOG_ERROR, TAG "Load video encoder config failed for %s. Sharing with main output.", config_->name.c_str());
            config_->videoConfig = OBS_STREAMING_ENC_PLACEHOLDER;
            return GetVideoEncoder();
        }

        OBSDataAutoRelease settings = obs_data_create_from_json(videoConfig->encoderParams.dump().c_str());
        if (!settings)
            settings = obs_data_create();
        // Let the service enforce what it needs (keyframes, B-frames for WHIP, ...).
        if (auto service = CurrentService())
            obs_service_apply_encoder_settings(service, settings, nullptr);

        auto name = VideoEncoderName(videoConfig->encoderId);
        OBSEncoderAutoRelease enc = obs_get_encoder_by_name(name.c_str());
        if (enc) {
            // Shared with another target. Settings can only change while it is idle.
            if (!obs_encoder_active(enc))
                obs_encoder_update(enc, settings);
        } else {
            enc = obs_video_encoder_create(videoConfig->encoderId.c_str(), name.c_str(), settings, nullptr);
        }
        if (enc && !obs_encoder_active(enc)) {
            auto wh = ParseResolution(videoConfig->resolution);
            if (wh.has_value()) {
                obs_encoder_set_gpu_scale_type(enc, obs_scale_type::OBS_SCALE_BICUBIC);
                auto [w, h] = *wh;
                obs_encoder_set_scaled_size(enc, w, h);
            }
            obs_encoder_set_frame_rate_divisor(enc, (std::max)(1, videoConfig->fpsDenumerator));
        }

        using_main_video_encoder_ = false;
        return enc.Get();
    }

    OBSEncoder GetAudioEncoder(int trackIdx = 0, std::optional<int> mixerId = std::nullopt) {
        auto config_id = config_->audioConfig.value_or(OBS_STREAMING_ENC_PLACEHOLDER);
        if (config_id == "" || config_id == OBS_STREAMING_ENC_PLACEHOLDER) {
            OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();
            OBSEncoder enc = obs_output_get_audio_encoder(stream_output, 0);
            using_main_audio_encoder_ = true;
            return enc.Get();
        } else if (config_id == OBS_RECORDING_ENC_PLACEHOLDER) {
            OBSOutputAutoRelease stream_output = obs_frontend_get_recording_output();
            OBSEncoder enc = obs_output_get_audio_encoder(stream_output, 0);
            using_main_audio_encoder_ = true;
            return enc.Get();
        }

        auto& global = GlobalMultiOutputConfig();
        auto audioConfig = FindById(global.audioConfig, config_id);
        if (!audioConfig) {
            blog(LOG_ERROR, TAG "Load audio encoder config failed for %s. Sharing with main output.", config_->name.c_str());
            config_->audioConfig = OBS_STREAMING_ENC_PLACEHOLDER;
            return GetAudioEncoder();
        }

        // If we were provided with a mixerId, override the audioConfig's mixerId with it
        int mixer = mixerId.value_or(audioConfig->mixerId);

        OBSDataAutoRelease settings = obs_data_create_from_json(audioConfig->encoderParams.dump().c_str());
        if (!settings)
            settings = obs_data_create();
        if (auto service = CurrentService())
            obs_service_apply_encoder_settings(service, nullptr, settings);

        auto name = AudioEncoderName(audioConfig->encoderId, trackIdx);
        OBSEncoderAutoRelease enc = obs_get_encoder_by_name(name.c_str());
        if (enc && !obs_encoder_active(enc) && obs_encoder_get_mixer_index(enc) != (size_t)mixer) {
            // The mixer of an audio encoder is fixed at creation; make a new one.
            enc = nullptr;
            name += "-mixer-" + std::to_string(mixer);
            enc = obs_get_encoder_by_name(name.c_str());
        }
        if (enc) {
            if (!obs_encoder_active(enc))
                obs_encoder_update(enc, settings);
        } else {
            enc = obs_audio_encoder_create(audioConfig->encoderId.c_str(), name.c_str(), settings, mixer, nullptr);
        }

        using_main_audio_encoder_ = false;
        return enc.Get();
    }

    bool PrepareOutputEncoders(QString* error)
    {
        if (!output_) {
            blog(LOG_ERROR, TAG "Prepare output encoder before output object is created.");
            return false;
        }

        ReleaseOutputEncoder();

        auto& global = GlobalMultiOutputConfig();

        OBSEncoder venc = GetVideoEncoder();
        OBSEncoder aenc = GetAudioEncoder();

        std::vector<std::tuple<int, OBSEncoder>> additionalTracks;
        auto audioConfigId = config_->audioConfig;
        if (audioConfigId && !audioConfigId->empty() && !IsSpecialEncoder(*audioConfigId)) {
            auto audioConfig = FindById(global.audioConfig, *audioConfigId);

            if (!audioConfig) {
                blog(LOG_ERROR, TAG "Load audio encoder config failed for %s. Could not determine additional tracks.", config_->name.c_str());
            } else {
                additionalTracks.reserve(audioConfig->audioTracks.size());
                for (auto& track : audioConfig->audioTracks) {
                    if (track->output_track <= 0 || track->output_track >= (int)MAX_OUTPUT_AUDIO_ENCODERS)
                        continue;
                    OBSEncoder enc = GetAudioEncoder(track->output_track, track->mixer_track);
                    if (enc) {
                        // Record the output track index and the encoder for later when we set the encoders on the output
                        additionalTracks.push_back({ track->output_track, enc });
                    }
                }
            }
        }

        if (!aenc || !venc) {
            // Reusing OBS's encoder only works once OBS itself is streaming or recording.
            ReleaseOutputEncoder();
            if (error)
                *error = obs_module_text("Notice.GetEncoder");
            return false;
        }

        // Plain RTMP ingest on these platforms only takes H.264; HEVC/AV1 connects and
        // then gets dropped, which shows up as an endless reconnect loop.
        codecWarning_.clear();
        const auto& preset = tandem::FindPlatformPreset(config_->platform);
        const char* codec = obs_encoder_get_codec(venc);
        if (preset.h264Only && config_->protocol == "RTMP" && codec && strcmp(codec, "h264") != 0) {
            codecWarning_ = QString::fromUtf8(obs_module_text("Warn.CodecNotSupported"))
                .arg(QString::fromUtf8(preset.label), QString::fromUtf8(codec)).toUtf8().constData();
            blog(LOG_WARNING, TAG "%s: %s", config_->name.c_str(), codecWarning_.c_str());
        }

        // The output takes its own reference to each encoder.
        obs_output_set_audio_encoder(output_, aenc, 0);
        for (auto& track : additionalTracks) {
            auto trackIdx = std::get<0>(track);
            auto enc = std::get<1>(track);
            obs_output_set_audio_encoder(output_, enc, trackIdx);
        }
        obs_output_set_video_encoder(output_, venc);

        return true;
    }


    bool ReleaseOutputEncoder()
    {
        if (!output_)
            return true;
        else if (obs_output_active(output_) == false)
        {
            if (obs_output_get_video_encoder(output_))
                obs_output_set_video_encoder(output_, nullptr);

            for (size_t idx = 0; idx < MAX_OUTPUT_AUDIO_ENCODERS; ++idx) {
                if (obs_output_get_audio_encoder(output_, idx))
                    obs_output_set_audio_encoder(output_, nullptr, idx);
            }

            return true;
        }
        else {
            blog(LOG_ERROR, TAG "Release output while it is active.");
            return false;
        }
    }


    // Destroys the output object. Only call when it is not active, or on teardown.
    void ReleaseOutput()
    {
        if (!output_)
            return;

        DisconnectSignals(output_);
        ++startGeneration_;

        if (obs_output_active(output_) || starting_ || reconnecting_)
            obs_output_force_stop(output_);

        if (!obs_output_active(output_)) {
            ReleaseOutputService();
            ReleaseOutputEncoder();
        }
        obs_output_release(output_);
        output_ = nullptr;
        starting_ = false;
        reconnecting_ = false;

        ReleaseOutputSceneView();
    }


    void UpdateStreamStatus() {
        using namespace std::chrono;

        if (!output_)
            return;

        static const char* units[] = {
            "bps", "Kbps", "Mbps", "Gbps", "Tbps", "Pbps", "Ebps", "Zbps", "Ybps"
        };

        auto new_bytes = obs_output_get_total_bytes(output_);
        auto new_frames = obs_output_get_total_frames(output_);
        auto dropped = obs_output_get_frames_dropped(output_);
        auto now = clock::now();

        auto interval = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_info_time_).count();
        if (interval > 0 && new_bytes >= total_bytes_ && new_frames >= 0 && (uint64_t)new_frames >= total_frames_)
        {
            auto duration = now - begin_time_;
            auto hh = duration_cast<hours>(duration);
            duration -= hh;
            auto mm = duration_cast<minutes>(duration);
            duration -= mm;
            auto ss = duration_cast<seconds>(duration);
            duration -= ss;

            char strDuration[64] = { 0 };
            snprintf(strDuration, sizeof(strDuration), "%02d:%02d:%02d", (int)hh.count(), (int)mm.count(), (int)ss.count());

            char strFps[32] = { 0 };
            snprintf(strFps, sizeof(strFps), "%d FPS", static_cast<int>(std::round((new_frames - total_frames_) / interval)));

            auto bps = (new_bytes - total_bytes_) * 8 / interval;
            current_kbps_ = bps / 1000.0;
            auto strBps = [&]()-> std::string {
                if (bps > 0)
                {
                    int unitMaxIndex = sizeof(units) / sizeof(*units);
                    int unitIndex = static_cast<int>(log10(bps) / 3);
                    if (unitIndex >= unitMaxIndex)
                        unitIndex = unitMaxIndex - 1;
                    auto strVal = std::to_string(bps / pow(1000, unitIndex)).substr(0, 4);
                    if (!strVal.empty() && strVal.back() == '.')
                        strVal.pop_back();
                    return strVal + " " + units[unitIndex];
                }
                else
                {
                    return "0 bps";
                }
            }();

            std::string text = std::string(strDuration) + "  " + strBps + "  " + strFps;
            if (dropped > 0 && new_frames > 0) {
                char strDrop[64];
                snprintf(strDrop, sizeof(strDrop), "  %s %d (%.1f%%)", obs_module_text("Status.Dropped"), dropped,
                    100.0 * dropped / (double)(new_frames + dropped));
                text += strDrop;
            }
            if (!codecWarning_.empty())
                text += "\n" + codecWarning_;
            msg_->setText(QString::fromUtf8(text.c_str()));
        }

        total_frames_ = new_frames;
        total_bytes_ = new_bytes;
        last_info_time_ = now;
    }

public:
    PushWidgetImpl(const std::string& targetid, QWidget* parent = 0)
        : QWidget(parent)
        , targetid_(targetid)
    {
        QObject::setObjectName("push-widget");

        auto& global = GlobalMultiOutputConfig();
        config_ = FindById(global.targets, targetid_);
        if (!config_)
            return;

        timer_ = new QTimer(this);
        timer_->setInterval(std::chrono::milliseconds(1000));
        QObject::connect(timer_, &QTimer::timeout, [this]() {
            UpdateStreamStatus();
        });

        auto layout = new QGridLayout(this);
        {
            auto header = new QHBoxLayout();
            enabled_ = new QCheckBox(this);
            enabled_->setToolTip(obs_module_text("Tip.Enabled"));
            header->addWidget(enabled_);
            header->addWidget(name_ = new QLabel(obs_module_text("NewStreaming"), this), 1);
            layout->addLayout(header, 0, 0, 1, 3);
        }
        QObject::connect(enabled_, &QCheckBox::toggled, [this](bool on) {
            config_->syncStart = on;
            config_->syncStop = on;
            SaveMultiOutputConfig();
        });

        layout->addWidget(btn_ = new QPushButton(obs_module_text("Btn.Start"), this), 1, 0);
        QObject::connect(btn_, &QPushButton::clicked, [this]() {
            StartStop();
        });

        layout->addWidget(edit_btn_ = new QPushButton(obs_module_text("Btn.Edit"), this), 1, 1);
        QObject::connect(edit_btn_, &QPushButton::clicked, [this]() {
            ShowEditDlg();
        });

        layout->addWidget(remove_btn_ = new QPushButton(obs_module_text("Btn.Delete"), this), 1, 2);

        // Keep action buttons usable in narrow docks across OBS themes.
        for (auto button : { btn_, edit_btn_, remove_btn_ }) {
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            button->setMinimumWidth(0);
            button->setMinimumHeight(button->fontMetrics().height() + 8);
        }
        layout->setColumnStretch(0, 1);
        layout->setColumnStretch(1, 1);
        layout->setColumnStretch(2, 1);

        layout->addWidget(msg_ = new QLabel(u8"", this), 2, 0, 1, 3);
        msg_->setWordWrap(true);
        layout->addItem(new QSpacerItem(0, 10), 3, 0);
        setLayout(layout);

        LoadConfig();
    }

    ~PushWidgetImpl()
    {
        ReleaseOutput();
    }


    void StartStreaming() override {
        if (!config_ || IsBusy())
            return;

        // recreate output
        ReleaseOutput();
        ++startGeneration_;
        stoppedForLoop_ = false;
        quickDrops_.clear();

        auto protocolInfo = GetProtocolInfos()->GetInfo(config_->protocol.c_str());
        if (!protocolInfo) {
        	blog(LOG_ERROR, TAG "Invalid protocol \"%s\", maybe broken config file.", config_->protocol.c_str());
        	protocolInfo = GetProtocolInfos()->GetList();
        }
        auto output_id = protocolInfo->outputId;

        {
            OBSDataAutoRelease output_settings = obs_data_create_from_json(config_->outputParam.dump().c_str());
            // Unique names keep the OBS log and obs-websocket output list readable.
            auto outputName = "Tandem: " + config_->name + " (" + targetid_ + ")";
            output_ = obs_output_create(output_id, outputName.c_str(), output_settings, nullptr);
        }
        if (!output_) {
            SetMsg(obs_module_text("Error.StartOutput"));
            return;
        }
        SetMeAsHandler(output_);

        isUseDelay_ = false;
        auto profileConfig = obs_frontend_get_profile_config();
        if (profileConfig) {
            bool useDelay = config_get_bool(profileConfig, "Output", "DelayEnable");
            bool preserveDelay = config_get_bool(profileConfig, "Output", "DelayPreserve");
            int delaySec = (int)config_get_int(profileConfig, "Output", "DelaySec");
            obs_output_set_delay(output_,
                useDelay ? delaySec : 0,
                preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0
            );

            if (useDelay && delaySec > 0)
                isUseDelay_ = true;

            // Use the same reconnect policy as OBS's own stream (Settings > Advanced).
            bool reconnect = config_get_bool(profileConfig, "Output", "Reconnect");
            int retryDelay = (int)config_get_uint(profileConfig, "Output", "RetryDelay");
            int maxRetries = (int)config_get_uint(profileConfig, "Output", "MaxRetries");
            obs_output_set_reconnect_settings(output_, reconnect ? maxRetries : 0, (std::max)(1, retryDelay));
        }

        if (!PrepareOutputService())
        {
            SetMsg(obs_module_text("Error.CreateRtmpService"));
            ReleaseOutput();
            return;
        }

        QString encoderError;
        if (!PrepareOutputEncoders(&encoderError))
        {
            SetMsg(encoderError.isEmpty() ? QString(obs_module_text("Error.CreateEncoder")) : encoderError);
            ReleaseOutput();
            return;
        }

        if (!PrepareEncoderSource())
        {
            SetMsg(obs_module_text("Error.SceneNotExist"));
            ReleaseOutput();
            return;
        }

        if (!obs_output_start(output_))
        {
            QString msg = obs_module_text("Error.StartOutput");
            if (const char* err = obs_output_get_last_error(output_); err && *err)
                msg += QString("\n") + QString::fromUtf8(err);
            blog(LOG_WARNING, TAG "%s: failed to start output: %s", config_->name.c_str(),
                obs_output_get_last_error(output_) ? obs_output_get_last_error(output_) : "(no detail)");
            SetMsg(msg);
            ReleaseOutput();
            return;
        }
        starting_ = true;
        UpdateButtons();
    }

    void StopStreamingImpl(bool ask) {
        if (!IsBusy())
            return;

        if (starting_ || reconnecting_) {
            // Not connected yet (or waiting to retry): cancel right away.
            obs_output_force_stop(output_);
            return;
        }

        bool useForce = false;
        if (isUseDelay_ && ask) {
            auto res = QMessageBox(QMessageBox::Icon::Information,
                "?",
                obs_module_text("Ques.DropDelay"),
                QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No,
                this
            ).exec();
            if (res == QMessageBox::Yes)
                useForce = true;
        }

        if (!useForce)
            obs_output_stop(output_);
        else
            obs_output_force_stop(output_);
    }

    void StopStreaming() override {
        StopStreamingImpl(true);
    }

    void OnOBSEvent(obs_frontend_event ev) override
    {
        if (!config_)
            return;
        if (ev == obs_frontend_event::OBS_FRONTEND_EVENT_EXIT
            || ev == obs_frontend_event::OBS_FRONTEND_EVENT_PROFILE_CHANGING
        ) {
            Stop();
        } else if (ev == obs_frontend_event::OBS_FRONTEND_EVENT_STREAMING_STARTING) {
            if (!IsBusy() && config_->syncStart) {
                StartStreaming();
            }
        } else if (ev == obs_frontend_event::OBS_FRONTEND_EVENT_STREAMING_STOPPING
            || ev == obs_frontend_event::OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
            // STOPPED also covers the main stream being dropped by the server, which
            // never sends STOPPING.
            if (IsBusy() && config_->syncStop) {
                StopStreamingImpl(false);
            }
        }
    }

    void LoadConfig()
    {
        name_->setText(QString::fromUtf8(config_->name));
        const auto& preset = tandem::FindPlatformPreset(config_->platform);
        if (strcmp(preset.id, "custom") != 0)
            name_->setText(QString::fromUtf8(config_->name) + "  [" + preset.label + "]");
        QSignalBlocker block(enabled_);
        enabled_->setChecked(config_->syncStart);
    }

    void ResetInfo()
    {
        total_frames_ = 0;
        total_bytes_ = 0;
        current_kbps_ = 0;
        last_info_time_ = clock::now();
        msg_->setText("");
    }

    bool IsBusy() override
    {
        return output_ != nullptr && (starting_ || reconnecting_ || obs_output_active(output_));
    }

    bool IsEnabled() const override
    {
        return config_ && config_->syncStart;
    }

    double CurrentKbps() const override
    {
        return current_kbps_;
    }

    bool UsesOwnVideoEncoder() const override
    {
        if (!config_)
            return false;
        auto id = config_->videoConfig.value_or(OBS_STREAMING_ENC_PLACEHOLDER);
        return !id.empty() && !IsSpecialEncoder(id);
    }

    double EstimatedKbps() override
    {
        if (!config_)
            return 0;
        auto& global = GlobalMultiOutputConfig();
        double kbps = 0;

        auto vid = config_->videoConfig.value_or(OBS_STREAMING_ENC_PLACEHOLDER);
        auto aid = config_->audioConfig.value_or(OBS_STREAMING_ENC_PLACEHOLDER);
        bool mainEstimated = false;
        if (vid.empty() || IsSpecialEncoder(vid)) {
            kbps += EstimateMainStreamKbps();
            mainEstimated = true;
        } else if (auto vc = FindById(global.videoConfig, vid)) {
            kbps += vc->encoderParams.value("bitrate", 0);
        }
        if (!aid.empty() && !IsSpecialEncoder(aid)) {
            if (auto ac = FindById(global.audioConfig, aid))
                kbps += ac->encoderParams.value("bitrate", 0);
        } else if (!mainEstimated) {
            kbps += 160;
        }
        return kbps;
    }

    void UpdateButtons()
    {
        bool busy = IsBusy();
        btn_->setText(obs_module_text(busy ? "Status.Stop" : "Btn.Start"));
        btn_->setEnabled(true);
        remove_btn_->setEnabled(!busy);
    }

    void StartStop()
    {
        if (IsBusy())
        {
            StopStreaming();
            return;
        }

        StartStreaming();
    }

    void Stop() override
    {
        if (IsBusy())
        {
            obs_output_force_stop(output_);
        }
    }

    bool ShowEditDlg() override
    {
        std::unique_ptr<EditOutputWidget> dlg{ createEditOutputWidget(targetid_, (QMainWindow*)obs_frontend_get_main_window()) };

        if (dlg->exec() == QDialog::DialogCode::Accepted)
        {
            SaveMultiOutputConfig();
            LoadConfig();
            return true;
        }
        else
            return false;
    }

    void SetMsg(QString msg)
    {
        msg_->setText(msg);
        msg_->setToolTip(msg);
    }

    // Runs fn on the UI thread if this widget still exists and the output that
    // emitted the signal is still the current one.
    template<class F>
    void PostToUI(F&& fn)
    {
        QPointer<QObject> guard(this);
        auto self = this;
        auto generation = startGeneration_;
        GetGlobalService().RunInUIThread([guard, self, generation, fn = std::forward<F>(fn)]() {
            if (!guard || generation != self->startGeneration_)
                return;
            fn(self);
        });
    }

    // obs logical
    void OnStarting() override
    {
        PostToUI([](PushWidgetImpl* self) {
            self->begin_time_ = clock::now();
            self->UpdateButtons();
            self->SetMsg(obs_module_text("Status.Connecting"));
        });
    }

    void OnStarted() override
    {
        PostToUI([](PushWidgetImpl* self) {
            self->starting_ = false;
            self->reconnecting_ = false;
            self->connected_time_ = clock::now();
            self->UpdateButtons();
            self->SetMsg(obs_module_text("Status.Streaming"));

            self->ResetInfo();
            self->timer_->start();
        });
    }

    void OnReconnect() override
    {
        auto when = clock::now();
        PostToUI([when](PushWidgetImpl* self) {
            self->reconnecting_ = true;
            self->timer_->stop();
            self->current_kbps_ = 0;

            // OBS resets its retry counter after every successful connection, so a
            // server that accepts and then drops us right away loops forever. Stop
            // after several quick drops and tell the user why.
            if (when - self->connected_time_ < std::chrono::seconds(kQuickDropSeconds))
                self->quickDrops_.push_back(when);
            while (!self->quickDrops_.empty() && when - self->quickDrops_.front() > std::chrono::seconds(kQuickDropWindowSeconds))
                self->quickDrops_.pop_front();
            if (self->quickDrops_.size() >= kQuickDropLimit) {
                blog(LOG_WARNING, TAG "%s: connection dropped %d times shortly after connecting; stopping to avoid a reconnect loop.",
                    self->config_->name.c_str(), (int)self->quickDrops_.size());
                self->stoppedForLoop_ = true;
                obs_output_force_stop(self->output_);
                return;
            }

            self->UpdateButtons();
            self->SetMsg(obs_module_text("Status.Reconnecting"));
        });
    }

    void OnReconnected() override
    {
        PostToUI([](PushWidgetImpl* self) {
            self->reconnecting_ = false;
            self->starting_ = false;
            self->connected_time_ = clock::now();
            self->UpdateButtons();
            self->SetMsg(obs_module_text("Status.Streaming"));

            self->ResetInfo();
            self->timer_->start();
        });
    }

    void OnStopping() override
    {
        PostToUI([](PushWidgetImpl* self) {
            self->timer_->stop();
            self->UpdateButtons();
            self->SetMsg(obs_module_text("Status.Stopping"));
        });
    }

    void OnStopped(int code, std::string lastError) override
    {
        PostToUI([code, lastError](PushWidgetImpl* self) {
            self->starting_ = false;
            self->reconnecting_ = false;
            self->ResetInfo();
            self->timer_->stop();
            self->UpdateButtons();

            QString msg;
            switch(code)
            {
                case OBS_OUTPUT_SUCCESS:
                    break;
                case OBS_OUTPUT_BAD_PATH:
                    msg = obs_module_text("Error.WrongRTMPUrl");
                    break;
                case OBS_OUTPUT_CONNECT_FAILED:
                    msg = obs_module_text("Error.ServerConnect");
                    break;
                case OBS_OUTPUT_INVALID_STREAM:
                    msg = obs_module_text("Error.ServerHandshake");
                    break;
                case OBS_OUTPUT_ERROR:
                    msg = obs_module_text("Error.ServerRefuse");
                    break;
                case OBS_OUTPUT_DISCONNECTED:
                    msg = obs_module_text("Error.Disconnected");
                    break;
                case OBS_OUTPUT_UNSUPPORTED:
                    msg = obs_module_text("Error.Unsupported");
                    break;
                case OBS_OUTPUT_NO_SPACE:
                case OBS_OUTPUT_ENCODE_ERROR:
                    msg = obs_module_text("Error.Encode");
                    break;
                default:
                    msg = obs_module_text("Error.Unknown");
                    break;
            }
            if (self->stoppedForLoop_)
                msg = obs_module_text("Error.ReconnectLoop");
            if (!lastError.empty() && code != OBS_OUTPUT_SUCCESS)
                msg += "\n" + QString::fromUtf8(lastError.c_str());
            if (!self->codecWarning_.empty() && code != OBS_OUTPUT_SUCCESS)
                msg += "\n" + QString::fromUtf8(self->codecWarning_.c_str());
            self->SetMsg(msg);
            if (code != OBS_OUTPUT_SUCCESS)
                blog(LOG_WARNING, TAG "%s stopped with code %d%s%s", self->config_->name.c_str(), code,
                    lastError.empty() ? "" : ": ", lastError.c_str());
        });
    }

    void onDeactive() override
    {
        // "stop" fires while the output still counts as active; encoders can only be
        // detached once it has fully deactivated.
        PostToUI([](PushWidgetImpl* self) {
            self->ReleaseOutputEncoder();
            self->ReleaseOutputSceneView();
        });
    }
};

PushWidget* createPushWidget(const std::string& targetid, QWidget* parent) {
    return new PushWidgetImpl(targetid, parent);
}
