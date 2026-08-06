#include "battery_service.hpp"
#include <cstdio>

BatteryService::BatteryService(sdbus::IConnection* dbusConn)
    : dbusConn_(dbusConn) {}

bool BatteryService::init() {
    if (!dbusConn_) {
        std::fprintf(stderr, "BatteryService: no system bus connection\n");
        return false;
    }
    try {
        sdbus::ObjectPath displayDevicePath;
        auto managerProxy = sdbus::createProxy(*dbusConn_,
                                               sdbus::ServiceName{"org.freedesktop.UPower"},
                                               sdbus::ObjectPath{"/org/freedesktop/UPower"});
        managerProxy->callMethod("GetDisplayDevice")
                    .onInterface("org.freedesktop.UPower")
                    .storeResultsTo(displayDevicePath);

        deviceProxy_ = sdbus::createProxy(*dbusConn_,
                                          sdbus::ServiceName{"org.freedesktop.UPower"},
                                          displayDevicePath);

        bool isPresent = static_cast<bool>(deviceProxy_->getProperty("IsPresent")
                                                        .onInterface("org.freedesktop.UPower.Device"));
        uint32_t type = static_cast<uint32_t>(deviceProxy_->getProperty("Type")
                                                            .onInterface("org.freedesktop.UPower.Device"));

        if (!isPresent || type == 0) {
            std::printf("BatteryService: No display battery device present (type %u).\n", type);
            return false;
        }

        std::map<sdbus::PropertyName, sdbus::Variant> allProps = deviceProxy_->getAllProperties()
                                                                             .onInterface("org.freedesktop.UPower.Device");

        updateFromProperties(allProps);

        deviceProxy_->uponSignal("PropertiesChanged")
                    .onInterface("org.freedesktop.DBus.Properties")
                    .call([this](const std::string& interfaceName,
                                 const std::map<sdbus::PropertyName, sdbus::Variant>& changedProperties,
                                 const std::vector<std::string>& invalidatedProperties) {
                        this->onPropertiesChanged(interfaceName, changedProperties, invalidatedProperties);
                    });

        return true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "BatteryService: Failed to connect to UPower or query display device: %s\n", e.what());
        return false;
    }
}

bool BatteryService::tick() {
    if (dirty_) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}

void BatteryService::onPropertiesChanged(const std::string& interfaceName,
                                         const std::map<sdbus::PropertyName, sdbus::Variant>& changedProperties,
                                         const std::vector<std::string>& invalidatedProperties) {
    (void)invalidatedProperties;
    if (interfaceName == "org.freedesktop.UPower.Device") {
        updateFromProperties(changedProperties);
    }
}

void BatteryService::updateFromProperties(const std::map<sdbus::PropertyName, sdbus::Variant>& properties) {
    bool changed = false;

    static const sdbus::PropertyName keyPercentage{"Percentage"};
    static const sdbus::PropertyName keyState{"State"};
    static const sdbus::PropertyName keyTimeToEmpty{"TimeToEmpty"};
    static const sdbus::PropertyName keyStateFull{"TimeToFull"};

    if (properties.count(keyPercentage)) {
        double pct = static_cast<double>(properties.at(keyPercentage));
        int newPct = static_cast<int>(pct);
        if (newPct != current_.percentage) {
            current_.percentage = newPct;
            changed = true;
        }
    }

    if (properties.count(keyState)) {
        uint32_t stateVal = static_cast<uint32_t>(properties.at(keyState));
        BatteryInfo::State newState = BatteryInfo::Unknown;
        if (stateVal == 1) newState = BatteryInfo::Charging;
        else if (stateVal == 2) newState = BatteryInfo::Discharging;
        else if (stateVal == 4) newState = BatteryInfo::Full;

        if (newState != current_.state) {
            current_.state = newState;
            changed = true;
        }
    }

    if (properties.count(keyTimeToEmpty)) {
        int64_t t = static_cast<int64_t>(properties.at(keyTimeToEmpty));
        int newT = static_cast<int>(t);
        if (newT != current_.timeToEmpty) {
            current_.timeToEmpty = newT;
            changed = true;
        }
    }

    if (properties.count(keyStateFull)) {
        int64_t t = static_cast<int64_t>(properties.at(keyStateFull));
        int newT = static_cast<int>(t);
        if (newT != current_.timeToFull) {
            current_.timeToFull = newT;
            changed = true;
        }
    }

    if (changed) {
        dirty_ = true;
    }
}
