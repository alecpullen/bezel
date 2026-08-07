#pragma once
#include "service.hpp"
#include "mpris_art_loader.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

struct MprisInfo {
    std::string title;
    std::string artist;
    std::string artUrl;
    enum class Status { Playing, Paused, Stopped } status = Status::Stopped;
    std::string player;
};

class MprisService : public Service {
public:
    MprisService(sdbus::IConnection* conn, NVGcontext* vg);
    ~MprisService() override = default;

    bool init() override;
    bool tick() override;

    const MprisInfo& info() const { return current_; }
    void setNvgContext(NVGcontext* vg);
    int artHandle(NVGcontext* vg) const { return artLoader_.imageHandle(vg); }
    int artFd() const { return artLoader_.curlFd(); }
    int artTimeout() const { return artLoader_.curlTimeout(); }

private:
    void scanExistingPlayers();
    void onNameOwnerChanged(const std::string& name, const std::string& oldOwner,
                            const std::string& newOwner);
    void selectPlayer(const std::string& name, const std::string& owner);
    void refreshPlayer();
    void onPlayerPropertiesChanged(const std::string& iface,
                                   const std::map<sdbus::PropertyName, sdbus::Variant>& changed,
                                   const std::vector<std::string>& invalidated);

    sdbus::IConnection* conn_ = nullptr;
    std::unique_ptr<sdbus::IProxy> dbusProxy_;          // org.freedesktop.DBus for NameOwnerChanged
    std::unique_ptr<sdbus::IProxy> playerProxy_;        // current player
    MprisArtLoader artLoader_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> seenPlayers_;
    std::string currentPlayer_;
    MprisInfo current_;
    bool dirty_ = false;
};
