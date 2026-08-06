#pragma once
#include "service.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <map>
#include <string>
#include <vector>

struct BatteryInfo {
    int percentage = 0; // 0-100
    enum State { Unknown, Charging, Discharging, Full } state = Unknown;
    int timeToEmpty = 0; // seconds
    int timeToFull = 0;  // seconds
};

class BatteryService : public Service {
public:
    BatteryService(sdbus::IConnection* dbusConn);
    ~BatteryService() override = default;

    bool init() override;
    bool tick() override;

    const BatteryInfo& info() const { return current_; }

private:
    void onPropertiesChanged(const std::string& interfaceName,
                             const std::map<sdbus::PropertyName, sdbus::Variant>& changedProperties,
                             const std::vector<std::string>& invalidatedProperties);
    void updateFromProperties(const std::map<sdbus::PropertyName, sdbus::Variant>& properties);

    sdbus::IConnection* dbusConn_;
    std::unique_ptr<sdbus::IProxy> deviceProxy_;
    BatteryInfo current_;
    bool dirty_ = false;
};
