#include "idle_service.hpp"
#include <cstdio>

bool IdleService::init(ext_idle_notifier_v1* notifier, wl_seat* seat, const SessionConfig& cfg) {
    notifier_ = notifier;
    seat_     = seat;
    cfg_      = cfg;
    if (!notifier_ || !seat_) {
        fprintf(stderr, "IdleService: notifier or seat unavailable; disabled\n");
        return false;
    }
    inhibited_ = cfg_.inhibit_idle;
    recreateNotifiers();
    return true;
}

void IdleService::recreateNotifiers() {
    if (notif_) {
        ext_idle_notification_v1_destroy(notif_);
        notif_ = nullptr;
    }
    if (inhibited_ || !notifier_ || !seat_) return;

    notif_ = ext_idle_notifier_v1_get_idle_notification(
        notifier_, (uint32_t)(cfg_.idle_timeout_sec * 1000), seat_);
    static const ext_idle_notification_v1_listener listener = {
        .idled   = idled,
        .resumed = resumed,
    };
    ext_idle_notification_v1_add_listener(notif_, &listener, this);
}

void IdleService::setInhibited(bool b) {
    if (inhibited_ == b) return;
    inhibited_ = b;
    recreateNotifiers();
    // State may reset to Active on inhibition; surface a change.
    if (inhibited_ && state_ != IdleState::Active) {
        state_ = IdleState::Active;
    }
    notify();
}

void IdleService::idled(void* data, ext_idle_notification_v1*) {
    auto* self = static_cast<IdleService*>(data);
    if (self->state_ == IdleState::Idle) return;
    self->state_ = IdleState::Idle;
    self->notify();
}

void IdleService::resumed(void* data, ext_idle_notification_v1*) {
    auto* self = static_cast<IdleService*>(data);
    if (self->state_ == IdleState::Active) return;
    self->state_ = IdleState::Active;
    self->notify();
}
