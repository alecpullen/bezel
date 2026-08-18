#include "logind_service.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <cstdio>
#include <string>
#include <unistd.h>

LogindService::LogindService(sdbus::IConnection* systemBus)
    : systemBus_(systemBus) {}

LogindService::~LogindService() = default;

bool LogindService::init() {
    if (!systemBus_) {
        std::fprintf(stderr, "LogindService: no system bus connection\n");
        return false;
    }
    try {
        proxy_ = sdbus::createProxy(*systemBus_,
            sdbus::ServiceName{"org.freedesktop.login1"},
            sdbus::ObjectPath{"/org/freedesktop/login1"});
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "LogindService: createProxy failed: %s\n", e.what());
        return false;
    }

    can("CanPowerOff",  canPowerOff_);
    can("CanReboot",    canReboot_);
    can("CanSuspend",   canSuspend_);
    can("CanHibernate", canHibernate_);
    return true;
}

bool LogindService::can(const char* method, bool& out) {
    if (!proxy_) return false;
    try {
        std::string val;
        proxy_->callMethod(method).onInterface("org.freedesktop.login1.Manager")
                                 .storeResultsTo(val);
        // "yes" / "challenge" are available; "na" is not.
        out = (val == "yes" || val == "challenge");
        return true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "LogindService: %s failed: %s\n", method, e.what());
        out = false;
        return false;
    }
}

void LogindService::callVoid(const char* method) {
    if (!proxy_) return;
    try {
        proxy_->callMethod(method)
              .onInterface("org.freedesktop.login1.Manager")
              .withArguments(false);  // interactive=false
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "LogindService: %s failed: %s\n", method, e.what());
    }
}

void LogindService::powerOff()  { callVoid("PowerOff"); }
void LogindService::reboot()    { callVoid("Reboot"); }
void LogindService::suspend()   { callVoid("Suspend"); }
void LogindService::hibernate() { callVoid("Hibernate"); }

void LogindService::logout() {
    if (!proxy_) return;
    try {
        sdbus::ObjectPath sessionPath;
        proxy_->callMethod("GetSessionByPID")
              .onInterface("org.freedesktop.login1.Manager")
              .withArguments((uint32_t)getpid())
              .storeResultsTo(sessionPath);
        proxy_->callMethod("TerminateSession")
              .onInterface("org.freedesktop.login1.Manager")
              .withArguments(sessionPath);
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "LogindService: logout failed: %s\n", e.what());
    }
}
