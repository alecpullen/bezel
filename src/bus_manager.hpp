#pragma once
#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <vector>
#include <poll.h>

class BusManager {
public:
    BusManager();

    sdbus::IConnection* system() const { return systemBus_.get(); }
    sdbus::IConnection* session() const { return sessionBus_.get(); }

    std::vector<pollfd> pollFds() const;
    int pollTimeout() const;
    void processPending() const;

private:
    std::unique_ptr<sdbus::IConnection> systemBus_;
    std::unique_ptr<sdbus::IConnection> sessionBus_;
};
