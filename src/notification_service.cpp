#include "notification_service.hpp"
#include <algorithm>
#include <cstdio>
#include <tuple>

NotificationService::NotificationService(sdbus::IConnection& conn)
    : conn_(conn) {}

NotificationService::~NotificationService() {
    obj_.reset();
    try { conn_.releaseName(sdbus::ServiceName{"org.freedesktop.Notifications"}); }
    catch (...) {}
}

bool NotificationService::init() {
    try {
        obj_ = sdbus::createObject(conn_, sdbus::ObjectPath{"/org/freedesktop/Notifications"});

        obj_->addVTable(
            sdbus::registerMethod("GetCapabilities")
                .implementedAs([]() -> std::vector<std::string> {
                    return {"body", "actions", "icon-static"};
                }),

            sdbus::registerMethod("Notify")
                .implementedAs([this](std::string app_name, uint32_t replaces_id,
                                      std::string app_icon, std::string summary,
                                      std::string body, std::vector<std::string> actions,
                                      std::map<std::string, sdbus::Variant> hints,
                                      int32_t expire_timeout) -> uint32_t {
                    return onNotify(std::move(app_name), replaces_id,
                                    std::move(app_icon), std::move(summary),
                                    std::move(body), std::move(actions),
                                    hints, expire_timeout);
                }),

            sdbus::registerMethod("CloseNotification")
                .implementedAs([this](uint32_t id) {
                    closeNotification(id, 3); // reason 3 = CloseNotification called
                }),

            sdbus::registerMethod("GetServerInformation")
                .implementedAs([]() -> std::tuple<std::string, std::string,
                                                   std::string, std::string> {
                    return {"bezel", "bezel", "0.1", "1.2"};
                }),

            sdbus::registerSignal("NotificationClosed")
                .withParameters<uint32_t, uint32_t>(),

            sdbus::registerSignal("ActionInvoked")
                .withParameters<uint32_t, std::string>()

        ).forInterface("org.freedesktop.Notifications");

        conn_.requestName(sdbus::ServiceName{"org.freedesktop.Notifications"});
        return true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "NotificationService: init failed: %s\n", e.what());
        return false;
    }
}

bool NotificationService::tick() {
    auto now = std::chrono::steady_clock::now();
    auto it = active_.begin();
    while (it != active_.end()) {
        if (it->expire_timeout > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - it->arrived_at).count();
            if (elapsed >= it->expire_timeout) {
                uint32_t id = it->id;
                it = active_.erase(it);
                emitNotificationClosed(id, 1); // reason 1 = expired
                popPending();
                dirty_ = true;
                continue;
            }
        }
        ++it;
    }
    if (dirty_) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}

uint32_t NotificationService::onNotify(std::string app_name, uint32_t replaces_id,
                                        std::string app_icon, std::string summary,
                                        std::string body, std::vector<std::string> actions,
                                        const std::map<std::string, sdbus::Variant>& /*hints*/,
                                        int32_t expire_timeout) {
    int32_t effective_timeout = (expire_timeout < 0) ? kDefaultTimeoutMs : expire_timeout;

    if (replaces_id != 0) {
        auto it = std::find_if(active_.begin(), active_.end(),
                               [&](const Notification& n) { return n.id == replaces_id; });
        if (it != active_.end()) {
            it->app_name       = std::move(app_name);
            it->app_icon       = std::move(app_icon);
            it->summary        = std::move(summary);
            it->body           = std::move(body);
            it->actions        = std::move(actions);
            it->expire_timeout = effective_timeout;
            it->arrived_at     = std::chrono::steady_clock::now();
            dirty_ = true;
            return replaces_id;
        }
    }

    // Also check pending_ for replaces_id
    if (replaces_id != 0) {
        auto pit = std::find_if(pending_.begin(), pending_.end(),
                                [&](const Notification& n) { return n.id == replaces_id; });
        if (pit != pending_.end()) {
            pit->app_name       = std::move(app_name);
            pit->app_icon       = std::move(app_icon);
            pit->summary        = std::move(summary);
            pit->body           = std::move(body);
            pit->actions        = std::move(actions);
            pit->expire_timeout = effective_timeout;
            pit->arrived_at     = std::chrono::steady_clock::now();
            return replaces_id;
        }
    }

    Notification n;
    n.id             = nextId_++;
    n.app_name       = std::move(app_name);
    n.app_icon       = std::move(app_icon);
    n.summary        = std::move(summary);
    n.body           = std::move(body);
    n.actions        = std::move(actions);
    n.expire_timeout = effective_timeout;
    n.arrived_at     = std::chrono::steady_clock::now();
    uint32_t new_id  = n.id;

    if ((int)active_.size() >= kMaxVisible) {
        pending_.push_back(std::move(n));
    } else {
        active_.push_back(std::move(n));
        dirty_ = true;
    }
    return new_id;
}

void NotificationService::closeNotification(uint32_t id, uint32_t reason) {
    auto it = std::find_if(active_.begin(), active_.end(),
                           [&](const Notification& n) { return n.id == id; });
    if (it == active_.end()) {
        // May be in pending_ (e.g. D-Bus CloseNotification on a queued item)
        auto pit = std::find_if(pending_.begin(), pending_.end(),
                                [&](const Notification& n) { return n.id == id; });
        if (pit != pending_.end()) {
            pending_.erase(pit);
            emitNotificationClosed(id, reason);
        }
        return;
    }
    active_.erase(it);
    emitNotificationClosed(id, reason);
    popPending();
    dirty_ = true;
}

void NotificationService::popPending() {
    if (pending_.empty() || (int)active_.size() >= kMaxVisible) return;
    pending_.front().arrived_at = std::chrono::steady_clock::now();
    active_.push_back(std::move(pending_.front()));
    pending_.erase(pending_.begin());
}

void NotificationService::emitActionInvoked(uint32_t id, const std::string& action_key) {
    if (!obj_) return;
    try {
        obj_->emitSignal("ActionInvoked")
            .onInterface("org.freedesktop.Notifications")
            .withArguments(id, action_key);
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "NotificationService: ActionInvoked emit failed: %s\n", e.what());
    }
}

void NotificationService::emitNotificationClosed(uint32_t id, uint32_t reason) {
    if (!obj_) return;
    try {
        obj_->emitSignal("NotificationClosed")
            .onInterface("org.freedesktop.Notifications")
            .withArguments(id, reason);
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "NotificationService: emitSignal failed: %s\n", e.what());
    }
}
