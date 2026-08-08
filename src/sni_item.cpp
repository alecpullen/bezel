#include "sni_item.hpp"
#include <cstdio>

SniItem::SniItem(sdbus::IConnection* conn, SniIconLoader& iconLoader, const std::string& service)
    : conn_(conn), iconLoader_(iconLoader), service_(service) {}

SniItem::~SniItem() {
    if (imageHandle_ >= 0) iconLoader_.destroy(imageHandle_);
}

bool SniItem::init() {
    if (!conn_) return false;
    // Try /StatusNotifierItem first (modern), then /StatusBarNotifierItem (KDE4).
    static const char* paths[] = {"/StatusNotifierItem", "/StatusBarNotifierItem"};
    for (const char* path : paths) {
        try {
            proxy_ = sdbus::createProxy(*conn_, sdbus::ServiceName{service_},
                                         sdbus::ObjectPath{path});
            // Verify the proxy is reachable by reading Id (spec type 's').
            info_.id = static_cast<std::string>(proxy_->getProperty("Id")
                .onInterface("org.kde.StatusNotifierItem"));
            info_.service = service_;
            readProperties();
            reloadIcon();

            proxy_->uponSignal("NewIcon")
                .onInterface("org.kde.StatusNotifierItem")
                .call([this] { this->onNewIcon(); });
            proxy_->uponSignal("NewAttentionIcon")
                .onInterface("org.kde.StatusNotifierItem")
                .call([this] { this->onNewIcon(); });
            proxy_->uponSignal("NewStatus")
                .onInterface("org.kde.StatusNotifierItem")
                .call([this] { this->onNewStatus(); });
            proxy_->uponSignal("NewTitle")
                .onInterface("org.kde.StatusNotifierItem")
                .call([this] { this->onNewTitle(); });
            proxy_->uponSignal("NewToolTip")
                .onInterface("org.kde.StatusNotifierItem")
                .call([this] { this->onNewToolTip(); });

            return true;
        } catch (const sdbus::Error& e) {
            std::fprintf(stderr, "SniItem: proxy %s at %s failed: %s\n",
                         service_.c_str(), path, e.what());
            proxy_.reset();
        }
    }
    return false;
}

void SniItem::readProperties() {
    if (!proxy_) return;
    try {
        info_.title = static_cast<std::string>(proxy_->getProperty("Title")
            .onInterface("org.kde.StatusNotifierItem"));
    } catch (const sdbus::Error&) {}
    try {
        info_.iconName = static_cast<std::string>(proxy_->getProperty("IconName")
            .onInterface("org.kde.StatusNotifierItem"));
    } catch (const sdbus::Error&) {}
    try {
        info_.iconThemePath = static_cast<std::string>(proxy_->getProperty("IconThemePath")
            .onInterface("org.kde.StatusNotifierItem"));
    } catch (const sdbus::Error&) {}
    try {
        info_.status = static_cast<std::string>(proxy_->getProperty("Status")
            .onInterface("org.kde.StatusNotifierItem"));
    } catch (const sdbus::Error&) {}
    try {
        info_.category = static_cast<std::string>(proxy_->getProperty("Category")
            .onInterface("org.kde.StatusNotifierItem"));
    } catch (const sdbus::Error&) {}
    try {
        info_.itemIsMenu = static_cast<bool>(proxy_->getProperty("ItemIsMenu")
            .onInterface("org.kde.StatusNotifierItem"));
    } catch (const sdbus::Error&) { info_.itemIsMenu = false; }
}

void SniItem::reloadIcon() {
    if (imageHandle_ >= 0) {
        iconLoader_.destroy(imageHandle_);
        imageHandle_ = -1;
    }
    // Prefer IconPixmap, fall back to IconName.
    try {
        auto pixmaps = static_cast<std::vector<std::tuple<int32_t, int32_t, std::vector<uint8_t>>>>(
            proxy_->getProperty("IconPixmap")
                .onInterface("org.kde.StatusNotifierItem"));
        imageHandle_ = iconLoader_.fromPixmap(pixmaps);
    } catch (const sdbus::Error&) {}
    if (imageHandle_ < 0 && !info_.iconName.empty()) {
        imageHandle_ = iconLoader_.fromIconName(info_.iconName, info_.iconThemePath);
    }
    dirty_ = true;
}

void SniItem::onNewIcon() {
    reloadIcon();
}

void SniItem::destroyIcon() {
    if (imageHandle_ >= 0) {
        iconLoader_.destroy(imageHandle_);
        imageHandle_ = -1;
    }
}
void SniItem::onNewStatus() {
    try {
        std::string s = static_cast<std::string>(proxy_->getProperty("Status")
            .onInterface("org.kde.StatusNotifierItem"));
        if (s != info_.status) { info_.status = s; dirty_ = true; }
    } catch (const sdbus::Error&) {}
}
void SniItem::onNewTitle() {
    try {
        std::string t = static_cast<std::string>(proxy_->getProperty("Title")
            .onInterface("org.kde.StatusNotifierItem"));
        if (t != info_.title) { info_.title = t; dirty_ = true; }
    } catch (const sdbus::Error&) {}
}
void SniItem::onNewToolTip() {
    // ToolTip is a rich struct; for M8 we only use Title. No state change needed.
}

void SniItem::activate(int x, int y) {
    if (!proxy_) return;
    try {
        proxy_->callMethod("Activate")
            .onInterface("org.kde.StatusNotifierItem")
            .withArguments(x, y);
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "SniItem: Activate failed: %s\n", e.what());
    }
}

void SniItem::contextMenu(int x, int y) {
    if (!proxy_) return;
    try {
        proxy_->callMethod("ContextMenu")
            .onInterface("org.kde.StatusNotifierItem")
            .withArguments(x, y);
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "SniItem: ContextMenu failed: %s\n", e.what());
    }
}

bool SniItem::consumeDirty() {
    if (dirty_) {
        dirty_ = false;
        return true;
    }
    return false;
}
