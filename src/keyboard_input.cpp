#include "keyboard_input.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

KeyboardInput::~KeyboardInput() {
    if (kb_) wl_keyboard_release(kb_);
    if (state_)  xkb_state_unref(state_);
    if (keymap_) xkb_keymap_unref(keymap_);
    if (ctx_)    xkb_context_unref(ctx_);
}

bool KeyboardInput::init(wl_seat* seat) {
    if (!seat) return false;
    ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx_) {
        fprintf(stderr, "KeyboardInput: xkb_context_new failed\n");
        return false;
    }

    kb_ = wl_seat_get_keyboard(seat);
    if (!kb_) {
        fprintf(stderr, "KeyboardInput: wl_seat_get_keyboard failed\n");
        return false;
    }

    static const wl_keyboard_listener listener = {
        .keymap      = keymap,
        .enter       = enter,
        .leave       = leave,
        .key         = key,
        .modifiers   = modifiers,
        .repeat_info = repeat_info,
    };
    wl_keyboard_add_listener(kb_, &listener, this);
    return true;
}

void KeyboardInput::keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    auto* self = static_cast<KeyboardInput*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || fd < 0 || size == 0) {
        if (fd >= 0) close(fd);
        return;
    }
    char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map != MAP_FAILED) {
        xkb_keymap* newMap = xkb_keymap_new_from_string(
            self->ctx_, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(map, size);
        if (newMap) {
            if (self->state_)  xkb_state_unref(self->state_);
            if (self->keymap_) xkb_keymap_unref(self->keymap_);
            self->keymap_ = newMap;
            self->state_  = xkb_state_new(self->keymap_);
            if (!self->state_) fprintf(stderr, "KeyboardInput: xkb_state_new failed\n");
        } else {
            fprintf(stderr, "KeyboardInput: xkb_keymap_new_from_string failed\n");
        }
    } else {
        fprintf(stderr, "KeyboardInput: mmap keymap failed\n");
    }
    close(fd);
}

void KeyboardInput::enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void KeyboardInput::leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

void KeyboardInput::key(void* data, wl_keyboard*, uint32_t, uint32_t,
                        uint32_t key, uint32_t state) {
    auto* self = static_cast<KeyboardInput*>(data);
    if (!self->state_) return;

    xkb_keycode_t kc = key + 8; // evdev offset
    KeyEvent ev;
    ev.keysym = xkb_state_key_get_one_sym(self->state_, kc);
    ev.pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    ev.caps_lock = xkb_state_mod_name_is_active(
        self->state_, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_EFFECTIVE) != 0;

    if (ev.pressed && ev.keysym != XKB_KEY_NoSymbol) {
        int n = xkb_state_key_get_utf8(self->state_, kc, ev.text, sizeof(ev.text));
        if (n < 0) ev.text[0] = '\0';
    }

    if (self->handler_) self->handler_(ev);
}

void KeyboardInput::modifiers(void* data, wl_keyboard*, uint32_t,
                              uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
    auto* self = static_cast<KeyboardInput*>(data);
    if (self->state_)
        xkb_state_update_mask(self->state_, dep, lat, lock, 0, 0, group);
}

void KeyboardInput::repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}
