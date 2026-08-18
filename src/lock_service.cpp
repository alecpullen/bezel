#include "lock_service.hpp"
#include <cstdio>
#include <ctime>

LockService::LockService(PamAuth& pamAuth, const SessionConfig& cfg)
    : pamAuth_(pamAuth), cfg_(cfg) {}

bool LockService::tick() {
    // Drive cooldown expiry: when the cooldown elapses and we're still locked
    // (not authenticating), clear the cooldown so the user can retry.
    if (cooldownActive_ && state_ != State::Authenticating &&
        std::chrono::steady_clock::now() >= cooldownUntil_) {
        cooldownActive_ = false;
        statusMessage_.clear();
        notify();
        return true;
    }
    return false;
}

void LockService::beginLock(LockMode mode) {
    mode_ = mode;
    failedAttempts_ = 0;
    cooldownActive_ = false;
    cooldownUntil_ = {};
    statusMessage_.clear();
    transition((mode_ == LockMode::Hard) ? State::Locking : State::SoftLocked);
}

void LockService::onHardLocked() {
    if (state_ == State::Locking)
        transition(State::Locked);
}

void LockService::unlock() {
    // Only meaningful from a locked / authenticating state.
    if (state_ == State::Unlocked) return;
    pamAuth_.cancel();
    cooldownActive_ = false;
    statusMessage_.clear();
    transition(State::Unlocked);
}

void LockService::submitPassword(const std::string& pw) {
    if (!locked()) return;
    if (state_ == State::Authenticating) return;   // already in flight
    if (inCooldown()) return;                       // rate-limited

    pamAuth_.cancel();
    std::string user = cfg_.lock_user.empty() ? cfg_.systemUser() : cfg_.lock_user;
    if (user.empty()) {
        statusMessage_ = "No user to authenticate";
        notify();
        return;
    }
    if (!pamAuth_.authenticateAsync(user, pw)) {
        statusMessage_ = "Authentication error";
        startCooldown();
        notify();
        return;
    }
    transition(State::Authenticating);
}

void LockService::pollPam() {
    if (state_ != State::Authenticating) return;
    finishAuth(pamAuth_.pollResult());
}

void LockService::finishAuth(PamResult r) {
    // After a failed attempt, return to the locked state appropriate for the
    // active mode (hard lock -> Locked, soft lock -> SoftLocked).
    State back = (mode_ == LockMode::Hard) ? State::Locked : State::SoftLocked;
    switch (r) {
        case PamResult::Success:
            transition(State::Unlocked);
            break;
        case PamResult::AuthFail:
            ++failedAttempts_;
            startCooldown();
            statusMessage_ = "Incorrect password";
            transition(back);
            break;
        case PamResult::Error:
            statusMessage_ = "Authentication error";
            startCooldown();
            transition(back);
            break;
        case PamResult::Pending:
        default:
            break;
    }
}

void LockService::transition(State s) {
    if (state_ == s) {
        if (stateCb_) stateCb_();
        return;
    }
    state_ = s;
    notify();
    if (stateCb_) stateCb_();
}

bool LockService::inCooldown() const {
    return cooldownActive_ &&
           std::chrono::steady_clock::now() < cooldownUntil_;
}

void LockService::startCooldown() {
    // Escalating cooldown: 1s / 2s / 5s based on failed attempt count.
    int wait = (failedAttempts_ >= 3) ? 5 : (failedAttempts_ == 2 ? 2 : 1);
    cooldownUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(wait);
    cooldownActive_ = true;
    notify();
}
