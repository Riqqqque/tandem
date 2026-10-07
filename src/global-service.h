#pragma once

#include <functional>

class GlobalService
{
public:
    ~GlobalService() {}
    virtual bool RunInUIThread(std::function<void()> task) = 0;
};

GlobalService& GetGlobalService();

// True between OBS_FRONTEND_EVENT_FINISHED_LOADING and EXIT. Frontend output and
// service getters crash if called while OBS is still loading modules.
bool IsFrontendReady();
