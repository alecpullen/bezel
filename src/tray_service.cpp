#include "tray_service.hpp"
#include <cstdio>
#include <vector>

static constexpr const char* SERVICE_NAME = "org.kde.StatusNotifierWatcher";
static constexpr const char* OBJECT_PATH = "/StatusBarNotifierWatcher";
static constexpr const char* INTERFACE = "org.kde.StatusNotifierWatcher";
static constexpr const char* FREEDESKTOP_DBUS = "org.freedesktop.DBus";
static constexpr const char* FREEDESKTOP_PATH = "/org/freedesktop/DBus";

TrayService::TrayService(sdbus::IConnection* conn, NVGcontext* vg)
    : conn_(conn), vg_(vg), iconLoader_(vg) {}

TrayService::~TrayService() {
    if (conn_) {
        try { conn_->releaseName(sdbus::ServiceName{SERVICE_NAME}); }
        catch (...) {}
    }
}

void TrayService::reloadIcons(NVGcontext* vg) {
    if (!vg || vg == vg_) return;
    // Destroy all old handles while the loader is still bound to the old
    // context, so nvgDeleteImage runs against the context that owns them.
    for (auto& item : items_) {
        item->destroyIcon();
    }
    vg_ = vg;
    iconLoader_.setNvgContext(vg);
    for (auto& item : items_) {
        item->reloadIcon();
    }
    dirty_ = true;
}

void TrayService::setNvgContext(NVGcontext* vg) {
    if (vg_ == nullptr && vg != nullptr) {
        vg_ = vg;
        iconLoader_.setNvgContext(vg);
        // Reload any icons that were queued before the context was set.
        for (auto& item : items_) {
            // Re-create items that have no image handle yet, since their
            // reloadIcon was attempted without a NanoVG context.
            if (item->imageHandle() < 0) {
                TrayItemInfo info = item->info();
                auto newItem = std::make_unique<SniItem>(conn_, iconLoader_, info.service);
                if (newItem->init()) {
                    item = std::move(newItem);
                }
            }
        }
        dirty_ = true;
    }
}

bool TrayService::init() {
    if (!conn_) {
        std::fprintf(stderr, "TrayService: no session bus connection\n");
        return false;
    }
    try {
        conn_->requestName(sdbus::ServiceName{SERVICE_NAME});
        std::fprintf(stderr, "TrayService: acquired %s\n", SERVICE_NAME);
        registerObject();

        // Watch NameOwnerChanged to detect vanishing clients.
        dbusProxy_ = sdbus::createProxy(*conn_,
            sdbus::ServiceName{FREEDESKTOP_DBUS},
            sdbus::ObjectPath{FREEDESKTOP_PATH});
        dbusProxy_->uponSignal("NameOwnerChanged")
            .onInterface(FREEDESKTOP_DBUS)
            .call([this](const std::string& name,
                         const std::string& oldOwner,
                         const std::string& newOwner) {
                this->onNameOwnerChanged(name, oldOwner, newOwner);
            });

        return true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "TrayService: failed to register %s: %s\n",
                     SERVICE_NAME, e.what());
        conn_ = nullptr;
        return false;
    }
}

void TrayService::registerObject() {
    object_ = sdbus::createObject(*conn_, sdbus::ObjectPath{OBJECT_PATH});

    object_->addVTable(
        sdbus::registerMethod("RegisterStatusNotifierItem")
            .implementedAs([this](const std::string& service) {
                this->onRegisterItem(service);
            }),
        sdbus::registerMethod("RegisterStatusNotifierHost")
            .implementedAs([](const std::string&) {
                // No-op: bezel is the host.
            }),
        sdbus::registerProperty("IsStatusNotifierHostRegistered")
            .withGetter([]() -> bool { return true; }),
        sdbus::registerProperty("ProtocolVersion")
            .withGetter([]() -> int32_t { return 0; }),
        sdbus::registerProperty("RegisteredStatusNotifierItems")
            .withGetter([this]() -> std::vector<std::string> {
                std::vector<std::string> names;
                for (const auto& item : items_)
                    names.push_back(item->info().service);
                return names;
            }),
        sdbus::registerSignal("StatusNotifierItemRegistered")
            .withParameters<std::string>(),
        sdbus::registerSignal("StatusNotifierUnregistered")
            .withParameters<std::string>()
    ).forInterface(INTERFACE);
}

void TrayService::onRegisterItem(const std::string& service) {
    if (service.empty()) return;
    // Check if already registered
    for (const auto& item : items_)
        if (item->info().service == service) return;

    auto item = std::make_unique<SniItem>(conn_, iconLoader_, service);
    if (!item->init()) {
        std::fprintf(stderr, "TrayService: SniItem init failed for %s\n", service.c_str());
        return;
    }
    items_.push_back(std::move(item));
    dirty_ = true;
    // Synchronously notify so observers (TrayWidget) drop stale icon refs
    // before the next poll iteration dispatches pointer events.
    notify();

    try {
        object_->emitSignal("StatusNotifierItemRegistered")
            .onInterface(INTERFACE)
            .withArguments(service);
    } catch (...) {}
}

void TrayService::onNameOwnerChanged(const std::string& name,
                                      const std::string& /*oldOwner*/,
                                      const std::string& newOwner) {
    if (newOwner.empty()) {
        // A service vanished. Check if it's one of our items.
        removeItem(name);
    }
}

void TrayService::removeItem(const std::string& service) {
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        if ((*it)->info().service == service) {
            it = items_.erase(it);
            dirty_ = true;
            // Synchronously notify so observers (TrayWidget) rebuild before
            // any pointer event can deref the freed SniItem this iteration.
            notify();
            try {
                object_->emitSignal("StatusNotifierUnregistered")
                    .onInterface(INTERFACE)
                    .withArguments(service);
            } catch (...) {}
            return;
        }
    }
}

bool TrayService::tick() {
    bool anyDirty = dirty_;
    for (auto& item : items_) {
        if (item->consumeDirty()) anyDirty = true;
    }
    if (anyDirty) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}
