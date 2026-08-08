#pragma once
#include "sni_icon_loader.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct TrayItemInfo {
    std::string service;
    std::string title;
    std::string iconName;
    std::string iconThemePath;
    std::string status;        // "Active" | "Passive" | "NeedsAttention"
    std::string category;
    std::string id;
    bool itemIsMenu = false;
};

class SniItem {
public:
    SniItem(sdbus::IConnection* conn, SniIconLoader& iconLoader, const std::string& service);
    ~SniItem();

    bool init();
    const TrayItemInfo& info() const { return info_; }
    int imageHandle() const { return imageHandle_; }
    void activate(int x, int y);
    void contextMenu(int x, int y);
    bool consumeDirty();

private:
    void readProperties();
    void reloadIcon();
    void onNewIcon();
    void onNewStatus();
    void onNewTitle();
    void onNewToolTip();

    sdbus::IConnection* conn_;
    SniIconLoader& iconLoader_;
    std::string service_;
    std::unique_ptr<sdbus::IProxy> proxy_;
    TrayItemInfo info_;
    int imageHandle_ = -1;
    bool dirty_ = true;
};
