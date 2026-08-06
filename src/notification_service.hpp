#pragma once
#include "service.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <chrono>
#include <map>
#include <string>
#include <vector>

struct Notification {
    uint32_t id = 0;
    std::string app_name;
    std::string app_icon;
    std::string summary;
    std::string body;
    std::vector<std::pair<std::string, std::string>> actions; // key, label
    int32_t expire_timeout_ms = 5000;
    std::chrono::steady_clock::time_point created_at;
    bool hovered = false;
};

class NotificationService : public Service {
public:
    explicit NotificationService(sdbus::IConnection* conn);
    ~NotificationService() override;

    bool init() override;
    bool tick() override;

    const std::map<uint32_t, Notification>& notifications() const { return notifications_; }

    void closeNotification(uint32_t id, uint32_t reason);
    void invokeAction(uint32_t id, const std::string& action_key);
    void setHovered(uint32_t id, bool hovered);

private:
    void registerObject();
    uint32_t notify(const std::string& app_name, uint32_t replaces_id,
                    const std::string& app_icon, const std::string& summary,
                    const std::string& body,
                    const std::vector<std::string>& actions,
                    const std::map<std::string, sdbus::Variant>& hints,
                    int32_t expire_timeout);

    sdbus::IConnection* conn_ = nullptr;
    std::unique_ptr<sdbus::IObject> object_;
    std::map<uint32_t, Notification> notifications_;
    uint32_t nextId_ = 1;
    bool dirty_ = false;
};
