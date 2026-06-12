#include "input_backend.hpp"
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

InputBackend::InputBackend(KeyCallback onKey, TextCallback onText, LeaveCallback onLeave)
    : onKey_(std::move(onKey)), onText_(std::move(onText)), onLeave_(std::move(onLeave)) {}

InputBackend::~InputBackend() {
    release();
    if (xkb_ctx_) { xkb_context_unref(xkb_ctx_); xkb_ctx_ = nullptr; }
}

void InputBackend::bind(wl_seat* seat) {
    if (keyboard_) return;
    if (!xkb_ctx_)
        xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    keyboard_ = wl_seat_get_keyboard(seat);
    static const wl_keyboard_listener listener = {
        .keymap      = kbd_keymap,
        .enter       = kbd_enter,
        .leave       = kbd_leave,
        .key         = kbd_key,
        .modifiers   = kbd_modifiers,
        .repeat_info = kbd_repeat_info,
    };
    wl_keyboard_add_listener(keyboard_, &listener, this);
}

void InputBackend::release() {
    if (xkb_state_) { xkb_state_unref(xkb_state_); xkb_state_ = nullptr; }
    if (xkb_map_)   { xkb_keymap_unref(xkb_map_);  xkb_map_   = nullptr; }
    if (keyboard_)  { wl_keyboard_release(keyboard_); keyboard_ = nullptr; }
}

void InputBackend::kbd_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    auto* self = static_cast<InputBackend*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !self->xkb_ctx_) {
        close(fd);
        return;
    }
    char* str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    if (str == MAP_FAILED) { close(fd); return; }

    if (self->xkb_state_) { xkb_state_unref(self->xkb_state_); self->xkb_state_ = nullptr; }
    if (self->xkb_map_)   { xkb_keymap_unref(self->xkb_map_);  self->xkb_map_   = nullptr; }

    self->xkb_map_ = xkb_keymap_new_from_string(self->xkb_ctx_, str,
                         XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(str, size);
    close(fd);

    if (self->xkb_map_)
        self->xkb_state_ = xkb_state_new(self->xkb_map_);
    else
        std::fprintf(stderr, "InputBackend: failed to compile XKB keymap\n");
}

void InputBackend::kbd_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {
    // No action needed; keyboard focus is tracked via the launcher mode flag on Panel.
}

void InputBackend::kbd_leave(void* data, wl_keyboard*, uint32_t, wl_surface*) {
    // Auto-dismiss the launcher when the compositor moves keyboard focus away.
    auto* self = static_cast<InputBackend*>(data);
    if (self->onLeave_) self->onLeave_();
}

void InputBackend::kbd_key(void* data, wl_keyboard*, uint32_t /*serial*/, uint32_t /*time*/,
                            uint32_t key, uint32_t state) {
    auto* self = static_cast<InputBackend*>(data);
    if (!self->xkb_state_ || state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

    // Wayland keycodes are XKB keycode - 8
    xkb_keycode_t keycode = key + 8;

    // Dispatch keysym(s) for special keys (Esc, Return, Backspace, arrows, etc.)
    const xkb_keysym_t* syms = nullptr;
    int nsyms = xkb_state_key_get_syms(self->xkb_state_, keycode, &syms);
    for (int i = 0; i < nsyms; ++i)
        if (self->onKey_) self->onKey_(syms[i]);

    // Dispatch UTF-8 text for printable characters only
    char buf[32] = {};
    int len = xkb_state_key_get_utf8(self->xkb_state_, keycode, buf, sizeof(buf));
    if (len > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f)
        if (self->onText_) self->onText_(buf);
}

void InputBackend::kbd_modifiers(void* data, wl_keyboard*, uint32_t /*serial*/,
                                  uint32_t mods_dep, uint32_t mods_lat,
                                  uint32_t mods_lock, uint32_t group) {
    auto* self = static_cast<InputBackend*>(data);
    if (self->xkb_state_)
        xkb_state_update_mask(self->xkb_state_, mods_dep, mods_lat, mods_lock, 0, 0, group);
}

void InputBackend::kbd_repeat_info(void*, wl_keyboard*, int32_t /*rate*/, int32_t /*delay*/) {
    // Key repeat is not implemented in this milestone.
}
