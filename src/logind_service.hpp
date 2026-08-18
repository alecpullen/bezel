#pragma once
#include "service.hpp"
#include <memory>

namespace sdbus { class IConnection; class IProxy; }

class LogindService : public Service {
public:
    LogindService(sdbus::IConnection* systemBus);
    ~LogindService() override;

    bool init() override;
    bool tick() override { return false; }

    bool canPowerOff()  const { return canPowerOff_; }
    bool canReboot()    const { return canReboot_; }
    bool canSuspend()   const { return canSuspend_; }
    bool canHibernate() const { return canHibernate_; }

    void powerOff();
    void reboot();
    void suspend();
    void hibernate();
    void logout();

private:
    bool can(const char* method, bool& out);
    void callVoid(const char* method);

    sdbus::IConnection* systemBus_ = nullptr;
    std::unique_ptr<sdbus::IProxy> proxy_;
    bool canPowerOff_  = false;
    bool canReboot_    = false;
    bool canSuspend_   = false;
    bool canHibernate_ = false;
};
