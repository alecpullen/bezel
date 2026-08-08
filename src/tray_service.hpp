#pragma once
#include "service.hpp"
#include "sni_item.hpp"
#include "sni_icon_loader.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <string>
#include <vector>

struct NVGcontext;

class TrayService : public Service {
public:
    TrayService(sdbus::IConnection* conn, NVGcontext* vg);
    ~TrayService() override;

    bool init() override;
    bool tick() override;

    void setNvgContext(NVGcontext* vg);
    void reloadIcons(NVGcontext* vg);

    const std::vector<std::unique_ptr<SniItem>>& items() const { return items_; }
    SniIconLoader& iconLoader() { return iconLoader_; }

private:
    void registerObject();
    void onRegisterItem(const std::string& service);
    void onNameOwnerChanged(const std::string& name,
                            const std::string& oldOwner,
                            const std::string& newOwner);
    void removeItem(const std::string& service);

    sdbus::IConnection* conn_ = nullptr;
    NVGcontext* vg_ = nullptr;
    std::unique_ptr<sdbus::IObject> object_;
    std::unique_ptr<sdbus::IProxy> dbusProxy_;
    std::vector<std::unique_ptr<SniItem>> items_;
    SniIconLoader iconLoader_;
    bool dirty_ = false;
};
