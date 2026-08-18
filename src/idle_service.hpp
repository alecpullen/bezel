#pragma once
#include "service.hpp"
#include "config.hpp"
#include <wayland-client.h>
#include "ext-idle-notify-v1-client-protocol.h"

enum class IdleState { Active, Idle };

class IdleService : public Service {
public:
    // Satisfies the pure-virtual Service::init() (no-op; use the arg overload).
    bool init() override { return true; }
    bool init(ext_idle_notifier_v1* notifier, wl_seat* seat, const SessionConfig& cfg);
    bool tick() override { return false; }   // event-driven via wayland callbacks
    void setInhibited(bool b);
    bool inhibited() const { return inhibited_; }
    IdleState state() const { return state_; }
private:
    void recreateNotifiers();
    static void idled(void*, ext_idle_notification_v1*);
    static void resumed(void*, ext_idle_notification_v1*);
    ext_idle_notifier_v1*     notifier_  = nullptr;
    wl_seat*                  seat_      = nullptr;
    ext_idle_notification_v1* notif_     = nullptr;
    SessionConfig             cfg_{};
    bool                      inhibited_ = false;
    IdleState                 state_     = IdleState::Active;
};
