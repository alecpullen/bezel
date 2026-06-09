#include "toplevel_service.hpp"
#include <cstring>

Toplevel::Toplevel(zwlr_foreign_toplevel_handle_v1* handle, std::function<void()> on_change)
    : handle_(handle), on_change_(on_change) {
    static const zwlr_foreign_toplevel_handle_v1_listener listener = {
        .title = on_title,
        .app_id = on_app_id,
        .output_enter = on_output_enter,
        .output_leave = on_output_leave,
        .state = on_state,
        .done = on_done,
        .closed = on_closed,
        .parent = on_parent,
    };
    zwlr_foreign_toplevel_handle_v1_add_listener(handle_, &listener, this);
}

Toplevel::~Toplevel() {
    if (!closed_) {
        zwlr_foreign_toplevel_handle_v1_destroy(handle_);
    }
}

void Toplevel::on_title(void* data, zwlr_foreign_toplevel_handle_v1*, const char* title) {
    auto* self = static_cast<Toplevel*>(data);
    self->info_.title = title ? title : "";
}

void Toplevel::on_app_id(void* data, zwlr_foreign_toplevel_handle_v1*, const char* app_id) {
    auto* self = static_cast<Toplevel*>(data);
    self->info_.app_id = app_id ? app_id : "";
}

void Toplevel::on_output_enter(void* data, zwlr_foreign_toplevel_handle_v1*, wl_output* output) {
    auto* self = static_cast<Toplevel*>(data);
    self->info_.outputs.push_back(output);
}

void Toplevel::on_output_leave(void* data, zwlr_foreign_toplevel_handle_v1*, wl_output* output) {
    auto* self = static_cast<Toplevel*>(data);
    auto& outs = self->info_.outputs;
    outs.erase(std::remove(outs.begin(), outs.end(), output), outs.end());
}

void Toplevel::on_state(void* data, zwlr_foreign_toplevel_handle_v1*, wl_array* state) {
    auto* self = static_cast<Toplevel*>(data);
    self->info_.maximized = false;
    self->info_.minimized = false;
    self->info_.activated = false;
    self->info_.fullscreen = false;

    uint32_t* entry;
    auto data_ptr = static_cast<const uint32_t*>(state->data);
    size_t count = state->size / sizeof(uint32_t);
    for (size_t i = 0; i < count; ++i) {
        entry = const_cast<uint32_t*>(&data_ptr[i]);
        if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) {
            self->info_.maximized = true;
        } else if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
            self->info_.minimized = true;
        } else if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
            self->info_.activated = true;
        } else if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) {
            self->info_.fullscreen = true;
        }
    }
}

void Toplevel::on_done(void* data, zwlr_foreign_toplevel_handle_v1*) {
    auto* self = static_cast<Toplevel*>(data);
    if (self->on_change_) self->on_change_();
}

void Toplevel::on_closed(void* data, zwlr_foreign_toplevel_handle_v1*) {
    auto* self = static_cast<Toplevel*>(data);
    self->closed_ = true;
    if (self->on_change_) self->on_change_();
}

void Toplevel::on_parent(void* data, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1* parent) {
    auto* self = static_cast<Toplevel*>(data);
    self->info_.parent = parent;
}

ToplevelService::ToplevelService(zwlr_foreign_toplevel_manager_v1* manager)
    : manager_(manager) {
    static const zwlr_foreign_toplevel_manager_v1_listener listener = {
        .toplevel = on_toplevel,
        .finished = on_finished,
    };
    zwlr_foreign_toplevel_manager_v1_add_listener(manager_, &listener, this);
}

ToplevelService::~ToplevelService() {
    if (manager_) {
        zwlr_foreign_toplevel_manager_v1_destroy(manager_);
    }
}

bool ToplevelService::init() {
    return true;
}

bool ToplevelService::tick() {
    if (dirty_) {
        dirty_ = false;
        
        auto it = std::remove_if(toplevels_.begin(), toplevels_.end(),
            [](const std::unique_ptr<Toplevel>& t) {
                return t->is_closed();
            });
        
        if (it != toplevels_.end()) {
            toplevels_.erase(it, toplevels_.end());
        }
        
        notify();
        return true;
    }
    return false;
}

void ToplevelService::on_toplevel(void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* handle) {
    auto* self = static_cast<ToplevelService*>(data);
    self->toplevels_.push_back(std::make_unique<Toplevel>(handle, [self]() {
        self->dirty_ = true;
    }));
    self->dirty_ = true;
}

void ToplevelService::on_finished(void* data, zwlr_foreign_toplevel_manager_v1*) {
    auto* self = static_cast<ToplevelService*>(data);
    self->manager_ = nullptr;
}
