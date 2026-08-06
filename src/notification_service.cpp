#include "notification_service.hpp"
#include <cstdio>

static constexpr const char* SERVICE_NAME = "org.freedesktop.Notifications";
static constexpr const char* OBJECT_PATH = "/org/freedesktop/Notifications";
static constexpr const char* INTERFACE = "org.freedesktop.Notifications";

NotificationService::NotificationService(sdbus::IConnection* conn) : conn_(conn) {}

NotificationService::~NotificationService() {
    if (conn_) {
        try {
            conn_->releaseName(sdbus::ServiceName{SERVICE_NAME});
        } catch (...) {}
    }
}

bool NotificationService::init() {
    if (!conn_) {
        std::fprintf(stderr, "NotificationService: no session bus connection\n");
        return false;
    }
    try {
        conn_->requestName(sdbus::ServiceName{SERVICE_NAME});
        registerObject();
        return true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "NotificationService: failed to register: %s\n", e.what());
        conn_ = nullptr;
        return false;
    }
}

void NotificationService::registerObject() {
    object_ = sdbus::createObject(*conn_, sdbus::ObjectPath{OBJECT_PATH});

    object_->addVTable(
        sdbus::registerMethod("GetCapabilities")
            .implementedAs([]() -> std::vector<std::string> {
                return {"body", "actions", "icon-static"};
            }),
        sdbus::registerMethod("Notify")
            .implementedAs([this](const std::string& app_name,
                                  uint32_t replaces_id,
                                  const std::string& app_icon,
                                  const std::string& summary,
                                  const std::string& body,
                                  const std::vector<std::string>& actions,
                                  const std::map<std::string, sdbus::Variant>& hints,
                                  int32_t expire_timeout) -> uint32_t {
                return this->notify(app_name, replaces_id, app_icon, summary, body,
                                    actions, hints, expire_timeout);
            }),
        sdbus::registerMethod("CloseNotification")
            .implementedAs([this](uint32_t id) {
                this->closeNotification(id, 3); // 3 = closed by call
            }),
        sdbus::registerMethod("GetServerInformation")
            .implementedAs([]() -> std::tuple<std::string, std::string, std::string, std::string> {
                return {"bezel", "bezel", "0.1", "1.2"};
            }),
        sdbus::registerSignal("NotificationClosed")
            .withParameters<uint32_t, uint32_t>(),
        sdbus::registerSignal("ActionInvoked")
            .withParameters<uint32_t, std::string>()
    ).forInterface(INTERFACE);
}

uint32_t NotificationService::notify(const std::string& app_name, uint32_t replaces_id,
                                     const std::string& app_icon, const std::string& summary,
                                     const std::string& body,
                                     const std::vector<std::string>& actions,
                                     const std::map<std::string, sdbus::Variant>& hints,
                                     int32_t expire_timeout) {
    (void)hints;

    Notification n;
    n.id = (replaces_id > 0 && notifications_.count(replaces_id)) ? replaces_id : nextId_++;
    n.app_name = app_name;
    n.app_icon = app_icon;
    n.summary = summary;
    n.body = body;
    n.expire_timeout_ms = expire_timeout > 0 ? expire_timeout : 5000;
    n.created_at = std::chrono::steady_clock::now();

    for (size_t i = 0; i + 1 < actions.size(); i += 2) {
        n.actions.emplace_back(actions[i], actions[i + 1]);
    }

    notifications_[n.id] = std::move(n);
    dirty_ = true;
    return n.id;
}

bool NotificationService::tick() {
    auto now = std::chrono::steady_clock::now();
    bool expired = false;
    for (auto it = notifications_.begin(); it != notifications_.end();) {
        const auto& n = it->second;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - n.created_at).count();
        if (!n.hovered && elapsed >= n.expire_timeout_ms) {
            uint32_t id = it->first;
            it = notifications_.erase(it);
            try {
                object_->emitSignal("NotificationClosed")
                    .onInterface(INTERFACE)
                    .withArguments(id, 1u); // 1 = expired
            } catch (...) {}
            expired = true;
        } else {
            ++it;
        }
    }

    if (dirty_ || expired) {
        dirty_ = false;
        Service::notify();
        return true;
    }
    return false;
}

void NotificationService::closeNotification(uint32_t id, uint32_t reason) {
    auto it = notifications_.find(id);
    if (it == notifications_.end()) return;
    notifications_.erase(it);
    dirty_ = true;
    try {
        object_->emitSignal("NotificationClosed")
            .onInterface(INTERFACE)
            .withArguments(id, reason);
    } catch (...) {}
}

void NotificationService::invokeAction(uint32_t id, const std::string& action_key) {
    auto it = notifications_.find(id);
    if (it == notifications_.end()) return;
    try {
        object_->emitSignal("ActionInvoked")
            .onInterface(INTERFACE)
            .withArguments(id, action_key);
    } catch (...) {}
    closeNotification(id, 2); // 2 = dismissed by user
}
