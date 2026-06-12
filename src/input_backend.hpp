#pragma once
#include <functional>
#include <string>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

class InputBackend {
public:
    using KeyCallback    = std::function<void(uint32_t keysym)>;
    using TextCallback   = std::function<void(const std::string& text)>;
    using LeaveCallback  = std::function<void()>;

    InputBackend(KeyCallback onKey, TextCallback onText, LeaveCallback onLeave);
    ~InputBackend();

    void bind(wl_seat* seat);   // call when WL_SEAT_CAPABILITY_KEYBOARD is gained
    void release();             // call when WL_SEAT_CAPABILITY_KEYBOARD is lost

    bool bound() const { return keyboard_ != nullptr; }

private:
    static void kbd_keymap    (void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size);
    static void kbd_enter     (void*, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*);
    static void kbd_leave     (void*, wl_keyboard*, uint32_t serial, wl_surface*);
    static void kbd_key       (void*, wl_keyboard*, uint32_t serial, uint32_t time,
                                uint32_t key, uint32_t state);
    static void kbd_modifiers (void*, wl_keyboard*, uint32_t serial,
                                uint32_t mods_dep, uint32_t mods_lat,
                                uint32_t mods_lock, uint32_t group);
    static void kbd_repeat_info(void*, wl_keyboard*, int32_t rate, int32_t delay);

    wl_keyboard*  keyboard_   = nullptr;
    xkb_context*  xkb_ctx_   = nullptr;
    xkb_keymap*   xkb_map_   = nullptr;
    xkb_state*    xkb_state_ = nullptr;

    KeyCallback   onKey_;
    TextCallback  onText_;
    LeaveCallback onLeave_;
};
