#pragma once
#include "service.hpp"
#include "protocol.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

struct Rect {
    float x, y, w, h; // proportional 0.0–1.0 within the workspace box
};

struct WorkspaceInfo {
    uint32_t index;
    uint32_t state; // active, urgent, none
    uint32_t clients;
    bool focused;
    std::vector<Rect> tiles; // proportional window rects for tiling-map mode
};

struct OutputWorkspaceState {
    std::vector<WorkspaceInfo> workspaces;
    std::string layout_symbol;
    std::string title;
    std::string appid;
    bool active = false;
    bool is_fullscreen = false;
    bool is_floating = false;
};

class WorkspaceService : public Service {
public:
    WorkspaceService(zdwl_ipc_manager_v2* manager);
    ~WorkspaceService() override;

    bool init() override;
    bool tick() override;

    void add_output(wl_output* output);
    void remove_output(wl_output* output);

    const OutputWorkspaceState* get_output_state(wl_output* output) const;
    zdwl_ipc_output_v2* dwl_output(wl_output* output) const;

private:
    struct OutputData {
        WorkspaceService* service;
        wl_output* wl_output_ptr;
        zdwl_ipc_output_v2* dwl_output;
        OutputWorkspaceState current;
        OutputWorkspaceState pending;
    };

    static void on_tags(void* data, zdwl_ipc_manager_v2*, uint32_t amount);
    static void on_layout(void* data, zdwl_ipc_manager_v2*, const char* name);

    static void on_output_toggle_visibility(void* data, zdwl_ipc_output_v2*);
    static void on_output_active(void* data, zdwl_ipc_output_v2*, uint32_t active);
    static void on_output_tag(void* data, zdwl_ipc_output_v2*, uint32_t tag, uint32_t state, uint32_t clients, uint32_t focused);
    static void on_output_layout(void* data, zdwl_ipc_output_v2*, uint32_t layout);
    static void on_output_title(void* data, zdwl_ipc_output_v2*, const char* title);
    static void on_output_appid(void* data, zdwl_ipc_output_v2*, const char* appid);
    static void on_output_layout_symbol(void* data, zdwl_ipc_output_v2*, const char* layout);
    static void on_output_frame(void* data, zdwl_ipc_output_v2*);
    static void on_output_fullscreen(void* data, zdwl_ipc_output_v2*, uint32_t is_fullscreen);
    static void on_output_floating(void* data, zdwl_ipc_output_v2*, uint32_t is_floating);
    static void on_output_x(void* data, zdwl_ipc_output_v2*, int32_t x);
    static void on_output_y(void* data, zdwl_ipc_output_v2*, int32_t y);
    static void on_output_width(void* data, zdwl_ipc_output_v2*, int32_t width);
    static void on_output_height(void* data, zdwl_ipc_output_v2*, int32_t height);
    static void on_output_last_layer(void* data, zdwl_ipc_output_v2*, const char* last_layer);
    static void on_output_kb_layout(void* data, zdwl_ipc_output_v2*, const char* kb_layout);
    static void on_output_keymode(void* data, zdwl_ipc_output_v2*, const char* keymode);
    static void on_output_scalefactor(void* data, zdwl_ipc_output_v2*, uint32_t scalefactor);

    zdwl_ipc_manager_v2* manager_;
    std::map<wl_output*, std::unique_ptr<OutputData>> outputs_;
    uint32_t tag_count_ = 0;
    std::vector<std::string> layouts_;
    bool dirty_ = false;
};
