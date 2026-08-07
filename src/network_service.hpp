#pragma once
#include "service.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct NetworkInfo {
    enum class Type { None, Wired, Wifi } type = Type::None;
    std::string ssid;
    int strength = 0;       // 0–100, Wi-Fi only
};

class NetworkService : public Service {
public:
    explicit NetworkService(sdbus::IConnection* conn);
    ~NetworkService() override = default;

    bool init() override;
    bool tick() override;

    const NetworkInfo& info() const { return current_; }

private:
    void refresh();
    void onPropertiesChanged(const std::string& iface,
                             const std::map<sdbus::PropertyName, sdbus::Variant>& changed,
                             const std::vector<std::string>& invalidated);

    sdbus::IConnection* conn_ = nullptr;
    std::unique_ptr<sdbus::IProxy> managerProxy_;
    std::unique_ptr<sdbus::IProxy> activeConnProxy_;
    NetworkInfo current_;
    bool dirty_ = false;
};
