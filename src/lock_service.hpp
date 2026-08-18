#pragma once
#include "service.hpp"
#include "config.hpp"
#include "pam_auth.hpp"
#include "keyboard_input.hpp"
#include <chrono>
#include <functional>
#include <string>

enum class LockMode { Hard, Soft };

// Lock state machine. Owns the security-critical logic: PAM authentication
// (delegated to a shared PamAuth), the failed-attempt counter, escalating
// cooldown, and status messaging. Surface coordination (per-output LockOverlays)
// lives in App; this class only tracks state and drives PAM.
class LockService : public Service {
public:
    explicit LockService(PamAuth& pamAuth, const SessionConfig& cfg);
    bool init() override { return true; }

    bool tick() override;

    // App calls beginLock() after showing lock surfaces, and onHardLocked()
    // when the compositor confirms the session lock via the locked event.
    void beginLock(LockMode mode);
    void onHardLocked();
    void unlock();

    void submitPassword(const std::string& pw);
    void pollPam();   // App calls when resultFd() becomes readable

    bool locked() const { return state_ == State::Locked || state_ == State::SoftLocked; }
    bool authenticating() const { return state_ == State::Authenticating; }
    LockMode mode() const { return mode_; }

    int failedAttempts() const { return failedAttempts_; }
    std::string statusMessage() const { return statusMessage_; }
    int resultFd() const { return pamAuth_.resultFd(); }

    // Signalled to App when the state changes meaningfully.
    void setStateCallback(std::function<void()> cb) { stateCb_ = std::move(cb); }

private:
    enum class State { Unlocked, Locking, Locked, SoftLocked, Authenticating };
    void transition(State s);
    void finishAuth(PamResult r);
    bool inCooldown() const;
    void startCooldown();

    PamAuth& pamAuth_;
    SessionConfig cfg_;
    State   state_     = State::Unlocked;
    LockMode mode_      = LockMode::Hard;
    int     failedAttempts_ = 0;
    std::chrono::steady_clock::time_point cooldownUntil_{};
    bool    cooldownActive_ = false;
    std::string statusMessage_;
    std::function<void()> stateCb_;
};
