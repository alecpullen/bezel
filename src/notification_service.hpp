#pragma once
#include "service.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Notification {
    uint32_t    id;
    std::string app_name;
    std::string app_icon;
    std::string summary;
    std::string body;
    std::vector<std::string> actions;  // alternating key/label pairs
    int32_t     expire_timeout;        // ms; -1 = server default, 0 = never
    std::chrono::steady_clock::time_point arrived_at;
};

class NotificationService : public Service {
public:
    explicit NotificationService(sdbus::IConnection& conn);
    ~NotificationService() override;

    bool init() override;
    bool tick() override;

    const std::vector<Notification>& active() const { return active_; }

    void closeNotification(uint32_t id, uint32_t reason = 2);
    void emitActionInvoked(uint32_t id, const std::string& action_key);

private:
    void popPending();
    uint32_t onNotify(std::string app_name, uint32_t replaces_id,
                      std::string app_icon, std::string summary,
                      std::string body, std::vector<std::string> actions,
                      const std::map<std::string, sdbus::Variant>& hints,
                      int32_t expire_timeout);

    void emitNotificationClosed(uint32_t id, uint32_t reason);

    sdbus::IConnection&             conn_;
    std::unique_ptr<sdbus::IObject> obj_;

    std::vector<Notification> active_;
    std::vector<Notification> pending_;
    uint32_t                  nextId_ = 1;
    bool                      dirty_  = false;

    static constexpr int32_t kDefaultTimeoutMs = 5000;
    static constexpr int      kMaxVisible       = 3;
};
