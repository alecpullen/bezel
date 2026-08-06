#include "bus_manager.hpp"
#include <cstdio>

BusManager::BusManager() {
    try {
        systemBus_ = sdbus::createSystemBusConnection();
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "BusManager: failed to connect to system bus: %s\n", e.what());
    }
    try {
        sessionBus_ = sdbus::createSessionBusConnection();
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "BusManager: failed to connect to session bus: %s\n", e.what());
    }
}

std::vector<pollfd> BusManager::pollFds() const {
    std::vector<pollfd> fds;
    auto add = [&fds](sdbus::IConnection* conn) {
        if (!conn) return;
        auto data = conn->getEventLoopPollData();
        fds.push_back({data.fd, data.events, 0});
        if (data.eventFd >= 0) {
            fds.push_back({data.eventFd, POLLIN, 0});
        }
    };
    add(systemBus_.get());
    add(sessionBus_.get());
    return fds;
}

int BusManager::pollTimeout() const {
    int timeout = -1;
    auto consider = [&timeout](sdbus::IConnection* conn) {
        if (!conn) return;
        auto data = conn->getEventLoopPollData();
        int t = data.getPollTimeout();
        if (t >= 0 && (timeout < 0 || t < timeout)) timeout = t;
    };
    consider(systemBus_.get());
    consider(sessionBus_.get());
    return timeout;
}

void BusManager::processPending() const {
    if (systemBus_) systemBus_->processPendingEvent();
    if (sessionBus_) sessionBus_->processPendingEvent();
}
