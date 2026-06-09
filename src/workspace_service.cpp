#include "workspace_service.hpp"
#include <cstring>

WorkspaceService::WorkspaceService(zdwl_ipc_manager_v2* manager)
    : manager_(manager) {
    static const zdwl_ipc_manager_v2_listener listener = {
        .tags = on_tags,
        .layout = on_layout,
    };
    zdwl_ipc_manager_v2_add_listener(manager_, &listener, this);
}

WorkspaceService::~WorkspaceService() {
    outputs_.clear();
    if (manager_) {
        zdwl_ipc_manager_v2_destroy(manager_);
    }
}

bool WorkspaceService::init() {
    return true;
}

bool WorkspaceService::tick() {
    if (dirty_) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}

void WorkspaceService::add_output(wl_output* output) {
    if (outputs_.count(output)) return;

    auto data = std::make_unique<OutputData>();
    data->service = this;
    data->wl_output_ptr = output;
    data->dwl_output = zdwl_ipc_manager_v2_get_output(manager_, output);

    if (tag_count_ > 0) {
        data->pending.workspaces.resize(tag_count_);
        for (uint32_t i = 0; i < tag_count_; ++i)
            data->pending.workspaces[i].index = i;
    }

    static const zdwl_ipc_output_v2_listener listener = {
        .toggle_visibility = on_output_toggle_visibility,
        .active = on_output_active,
        .tag = on_output_tag,
        .layout = on_output_layout,
        .title = on_output_title,
        .appid = on_output_appid,
        .layout_symbol = on_output_layout_symbol,
        .frame = on_output_frame,
        .fullscreen = on_output_fullscreen,
        .floating = on_output_floating,
        .x = on_output_x,
        .y = on_output_y,
        .width = on_output_width,
        .height = on_output_height,
        .last_layer = on_output_last_layer,
        .kb_layout = on_output_kb_layout,
        .keymode = on_output_keymode,
        .scalefactor = on_output_scalefactor,
    };
    zdwl_ipc_output_v2_add_listener(data->dwl_output, &listener, data.get());

    outputs_[output] = std::move(data);
}

void WorkspaceService::remove_output(wl_output* output) {
    outputs_.erase(output);
}

const OutputWorkspaceState* WorkspaceService::get_output_state(wl_output* output) const {
    auto it = outputs_.find(output);
    if (it != outputs_.end()) {
        return &it->second->current;
    }
    return nullptr;
}

zdwl_ipc_output_v2* WorkspaceService::dwl_output(wl_output* output) const {
    auto it = outputs_.find(output);
    return it != outputs_.end() ? it->second->dwl_output : nullptr;
}

void WorkspaceService::on_tags(void* data, zdwl_ipc_manager_v2*, uint32_t amount) {
    auto* self = static_cast<WorkspaceService*>(data);
    self->tag_count_ = amount;
    for (auto& [wl_out, out_data] : self->outputs_) {
        out_data->pending.workspaces.resize(amount);
        for (uint32_t i = 0; i < amount; ++i) {
            out_data->pending.workspaces[i].index = i;
        }
    }
}

void WorkspaceService::on_layout(void* data, zdwl_ipc_manager_v2*, const char* name) {
    auto* self = static_cast<WorkspaceService*>(data);
    self->layouts_.push_back(name);
}

void WorkspaceService::on_output_toggle_visibility(void* data, zdwl_ipc_output_v2*) {
    (void)data;
}

void WorkspaceService::on_output_active(void* data, zdwl_ipc_output_v2*, uint32_t active) {
    auto* out_data = static_cast<OutputData*>(data);
    out_data->pending.active = (active != 0);
}

void WorkspaceService::on_output_tag(void* data, zdwl_ipc_output_v2*, uint32_t tag, uint32_t state, uint32_t clients, uint32_t focused) {
    auto* out_data = static_cast<OutputData*>(data);
    if (tag < out_data->pending.workspaces.size()) {
        auto& ws = out_data->pending.workspaces[tag];
        ws.state = state;
        ws.clients = clients;
        ws.focused = (focused != 0);
    }
}

void WorkspaceService::on_output_layout(void* data, zdwl_ipc_output_v2*, uint32_t layout) {
    (void)data; (void)layout;
}

void WorkspaceService::on_output_title(void* data, zdwl_ipc_output_v2*, const char* title) {
    auto* out_data = static_cast<OutputData*>(data);
    out_data->pending.title = title ? title : "";
}

void WorkspaceService::on_output_appid(void* data, zdwl_ipc_output_v2*, const char* appid) {
    auto* out_data = static_cast<OutputData*>(data);
    out_data->pending.appid = appid ? appid : "";
}

void WorkspaceService::on_output_layout_symbol(void* data, zdwl_ipc_output_v2*, const char* layout) {
    auto* out_data = static_cast<OutputData*>(data);
    out_data->pending.layout_symbol = layout ? layout : "";
}

void WorkspaceService::on_output_frame(void* data, zdwl_ipc_output_v2*) {
    auto* out_data = static_cast<OutputData*>(data);
    out_data->current = out_data->pending;
    out_data->service->dirty_ = true;
}

void WorkspaceService::on_output_fullscreen(void* data, zdwl_ipc_output_v2*, uint32_t is_fullscreen) {
    auto* out_data = static_cast<OutputData*>(data);
    out_data->pending.is_fullscreen = (is_fullscreen != 0);
}

void WorkspaceService::on_output_floating(void* data, zdwl_ipc_output_v2*, uint32_t is_floating) {
    auto* out_data = static_cast<OutputData*>(data);
    out_data->pending.is_floating = (is_floating != 0);
}

void WorkspaceService::on_output_x(void* data, zdwl_ipc_output_v2*, int32_t x) { (void)data; (void)x; }
void WorkspaceService::on_output_y(void* data, zdwl_ipc_output_v2*, int32_t y) { (void)data; (void)y; }
void WorkspaceService::on_output_width(void* data, zdwl_ipc_output_v2*, int32_t width) { (void)data; (void)width; }
void WorkspaceService::on_output_height(void* data, zdwl_ipc_output_v2*, int32_t height) { (void)data; (void)height; }
void WorkspaceService::on_output_last_layer(void* data, zdwl_ipc_output_v2*, const char* last_layer) { (void)data; (void)last_layer; }
void WorkspaceService::on_output_kb_layout(void* data, zdwl_ipc_output_v2*, const char* kb_layout) { (void)data; (void)kb_layout; }
void WorkspaceService::on_output_keymode(void* data, zdwl_ipc_output_v2*, const char* keymode) { (void)data; (void)keymode; }
void WorkspaceService::on_output_scalefactor(void* data, zdwl_ipc_output_v2*, uint32_t scalefactor) { (void)data; (void)scalefactor; }
