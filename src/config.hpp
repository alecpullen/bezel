#pragma once
#include <cstdint>
#include <string>

struct SessionConfig {
    int      idle_timeout_sec = 300;
    int      dim_timeout_sec  = 30;
    bool     lock_on_idle     = true;
    bool     inhibit_idle     = false;
    uint32_t lock_bg_color    = 0x1a1b26; // Tokyo Night bg
    float    lock_bg_opacity  = 0.95f;
    uint32_t lock_accent      = 0x7aa2f7; // Tokyo Night blue
    std::string lock_user;                // optional PAM user; empty = current user

    // Returns the user to authenticate: lock_user if set, else $LOGNAME/$USER,
    // else the passwd entry for the current euid.
    std::string systemUser() const;
};

// Reads [session] from ~/.config/bezel/config.toml if present; returns defaults otherwise.
SessionConfig loadSessionConfig();
