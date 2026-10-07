#include "pch.h"

class PushWidget : virtual public QWidget {
public:
    virtual ~PushWidget() {}
    virtual bool ShowEditDlg() = 0;
    virtual void StartStreaming() = 0;
    virtual void StopStreaming() = 0;
    // Force-stops the output without asking (exit, profile change, delete).
    virtual void Stop() = 0;
    virtual void OnOBSEvent(obs_frontend_event ev) = 0;
    virtual QPushButton* GetDeleteButton() = 0;
    virtual std::string GetTargetId() const = 0;
    // Connecting, streaming or reconnecting.
    virtual bool IsBusy() = 0;
    // Opted in to go live with OBS / "Start enabled".
    virtual bool IsEnabled() const = 0;
    // Measured upload while streaming, in kbps; 0 when idle.
    virtual double CurrentKbps() const = 0;
    // Planned upload from the encoder settings, in kbps; 0 when unknown.
    virtual double EstimatedKbps() = 0;
    // True when this target encodes separately instead of reusing an OBS encoder.
    virtual bool UsesOwnVideoEncoder() const = 0;
};

PushWidget* createPushWidget(const std::string& targetId, QWidget* parent = 0);

// Planned upload of OBS's own stream output in kbps, from the profile settings.
double EstimateMainStreamKbps();
