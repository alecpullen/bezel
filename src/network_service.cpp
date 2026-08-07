#include "network_service.hpp"
#include <cstdio>

NetworkService::NetworkService(sdbus::IConnection* conn)
    : conn_(conn) {}

bool NetworkService::init() {
    if (!conn_) {
        std::fprintf(stderr, "NetworkService: no system bus\n");
        return false;
    }
    try {
        managerProxy_ = sdbus::createProxy(*conn_,
            sdbus::ServiceName{"org.freedesktop.NetworkManager"},
            sdbus::ObjectPath{"/org/freedesktop/NetworkManager"});

        managerProxy_->uponSignal("PropertiesChanged")
            .onInterface("org.freedesktop.DBus.Properties")
            .call([this](const std::string& iface,
                         const std::map<sdbus::PropertyName, sdbus::Variant>& changed,
                         const std::vector<std::string>& invalidated) {
                this->onPropertiesChanged(iface, changed, invalidated);
            });

        refresh();
        return true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "NetworkService: init failed: %s\n", e.what());
        return false;
    }
}

void NetworkService::refresh() {
    if (!managerProxy_) return;
    try {
        // ActiveConnections is an array of object paths; take the first
        std::vector<sdbus::ObjectPath> conns =
            static_cast<std::vector<sdbus::ObjectPath>>(
                managerProxy_->getProperty("ActiveConnections")
                    .onInterface("org.freedesktop.NetworkManager"));
        if (conns.empty()) {
            if (current_.type != NetworkInfo::Type::None) {
                current_ = NetworkInfo{};
                dirty_ = true;
            }
            activeConnProxy_.reset();
            return;
        }
        sdbus::ObjectPath activePath = conns.front();

        if (!activeConnProxy_ || activeConnProxy_->getObjectPath() != activePath) {
            activeConnProxy_ = sdbus::createProxy(*conn_,
                sdbus::ServiceName{"org.freedesktop.NetworkManager"},
                activePath);

            activeConnProxy_->uponSignal("PropertiesChanged")
                .onInterface("org.freedesktop.DBus.Properties")
                .call([this](const std::string& iface,
                             const std::map<sdbus::PropertyName, sdbus::Variant>& changed,
                             const std::vector<std::string>& invalidated) {
                    this->onPropertiesChanged(iface, changed, invalidated);
                });
        }

        // Connection type
        sdbus::ObjectPath specificPath =
            static_cast<sdbus::ObjectPath>(
                activeConnProxy_->getProperty("SpecificObject")
                    .onInterface("org.freedesktop.NetworkManager.Connection.Active"));

        std::string typeStr =
            static_cast<std::string>(
                activeConnProxy_->getProperty("Type")
                    .onInterface("org.freedesktop.NetworkManager.Connection.Active"));

        NetworkInfo ni;
        if (typeStr == "802-11-wireless" || typeStr == "wifi") {
            ni.type = NetworkInfo::Type::Wifi;
            // Get SSID + strength from the access point
            if (!specificPath.empty() && specificPath != "/") {
                auto apProxy = sdbus::createProxy(*conn_,
                    sdbus::ServiceName{"org.freedesktop.NetworkManager"},
                    specificPath);
                std::vector<uint8_t> ssid =
                    static_cast<std::vector<uint8_t>>(
                        apProxy->getProperty("Ssid")
                            .onInterface("org.freedesktop.NetworkManager.AccessPoint"));
                ni.ssid.assign(ssid.begin(), ssid.end());
                uint8_t st = static_cast<uint8_t>(
                    apProxy->getProperty("Strength")
                        .onInterface("org.freedesktop.NetworkManager.AccessPoint"));
                ni.strength = static_cast<int>(st);
            }
        } else if (typeStr == "802-3-ethernet" || typeStr == "ethernet") {
            ni.type = NetworkInfo::Type::Wired;
        } else {
            ni.type = NetworkInfo::Type::None;
        }

        if (ni.type != current_.type || ni.ssid != current_.ssid || ni.strength != current_.strength) {
            current_ = ni;
            dirty_ = true;
        }
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "NetworkService: refresh failed: %s\n", e.what());
    }
}

void NetworkService::onPropertiesChanged(const std::string& iface,
                                         const std::map<sdbus::PropertyName, sdbus::Variant>& changed,
                                         const std::vector<std::string>& invalidated) {
    (void)iface; (void)changed; (void)invalidated;
    // Simplest correct behavior: re-fetch the active connection state.
    refresh();
}

bool NetworkService::tick() {
    if (dirty_) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}
