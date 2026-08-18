#pragma once
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <functional>
#include <cstdint>

struct KeyEvent {
    uint32_t keysym    = 0;      // XKB_KEY_*
    char     text[8]   = {0};    // UTF-8 (empty for non-text keys)
    bool     caps_lock = false;
    bool     pressed   = true;   // false = release
};

class KeyboardInput {
public:
    ~KeyboardInput();
    bool init(wl_seat* seat);                    // grabs wl_keyboard, sets listeners
    void setKeyHandler(std::function<void(const KeyEvent&)> h) { handler_ = std::move(h); }
private:
    static void keymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size);
    static void enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*);
    static void leave(void*, wl_keyboard*, uint32_t, wl_surface*);
    static void key(void*, wl_keyboard*, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void modifiers(void*, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group);
    static void repeat_info(void*, wl_keyboard*, int32_t rate, int32_t delay);

    wl_keyboard*   kb_     = nullptr;
    xkb_context*   ctx_    = nullptr;
    xkb_keymap*    keymap_ = nullptr;
    xkb_state*     state_  = nullptr;
    std::function<void(const KeyEvent&)> handler_;
};
