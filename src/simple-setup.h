// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - Simple mode: pick platforms, paste keys, press Go Live.
#pragma once

#include <functional>
#include <string>

class QWidget;
class PushWidget;

struct SimpleSetupHost {
	// Returns the push widget for a target id, creating it if the target was just added.
	std::function<PushWidget *(const std::string &targetId)> ensureWidget;
	// Called after Simple mode changed targets so the Advanced list and estimates refresh.
	std::function<void()> changed;
};

QWidget *CreateSimpleSetupWidget(SimpleSetupHost host, QWidget *parent);

// Target ids used by Simple mode; they are ordinary targets and also show in Advanced mode.
inline const char *const kSimpleTargetIds[] = {"simple-twitch", "simple-kick", "simple-youtube"};
inline const char *const kSimpleVideoConfigId = "simple-video";
inline const char *const kSimpleAudioConfigId = "simple-audio";
