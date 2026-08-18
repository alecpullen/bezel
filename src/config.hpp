#pragma once
#include <cstdint>

struct SessionConfig {
    int      idle_timeout_sec = 300;
    int      dim_timeout_sec  = 30;
    bool     lock_on_idle     = true;
    bool     inhibit_idle     = false;
    uint32_t lock_bg_color    = 0x1a1b26; // Tokyo Night bg
    float    lock_bg_opacity  = 0.95f;
    uint32_t lock_accent      = 0x7aa2f7; // Tokyo Night blue
};

// Reads [session] from ~/.config/bezel/config.toml if present; returns defaults otherwise.
SessionConfig loadSessionConfig();
