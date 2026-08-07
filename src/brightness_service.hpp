#pragma once
#include "service.hpp"
#include <string>

struct BrightnessInfo {
    float level = 0.0f;        // 0.0-1.0
    std::string device;
};

class BrightnessService : public Service {
public:
    BrightnessService() = default;
    ~BrightnessService() override;

    bool init() override;
    bool tick() override;

    const BrightnessInfo& info() const { return current_; }
    int inotifyFd() const { return inotifyFd_; }

private:
    bool readBrightness();
    BrightnessInfo current_;
    int inotifyFd_ = -1;
    int watchFd_ = -1;
    int maxBrightness_ = 0;
    bool dirty_ = false;
};
