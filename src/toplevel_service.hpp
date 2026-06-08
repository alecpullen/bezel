#pragma once
#include "service.hpp"
#include "protocol.hpp"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

struct ToplevelInfo {
    std::string title;
    std::string app_id;
    bool activated = false;
    bool maximized = false;
    bool minimized = false;
    bool fullscreen = false;
    std::vector<wl_output*> outputs;

    bool is_on_output(wl_output* output) const {
        return std::find(outputs.begin(), outputs.end(), output) != outputs.end();
    }
};

class Toplevel {
public:
    Toplevel(zwlr_foreign_toplevel_handle_v1* handle, std::function<void()> on_change);
    ~Toplevel();

    const ToplevelInfo& info() const { return info_; }
    zwlr_foreign_toplevel_handle_v1* handle() const { return handle_; }
    bool is_closed() const { return closed_; }

private:
    static void on_title(void* data, zwlr_foreign_toplevel_handle_v1*, const char* title);
    static void on_app_id(void* data, zwlr_foreign_toplevel_handle_v1*, const char* app_id);
    static void on_output_enter(void* data, zwlr_foreign_toplevel_handle_v1*, wl_output* output);
    static void on_output_leave(void* data, zwlr_foreign_toplevel_handle_v1*, wl_output* output);
    static void on_state(void* data, zwlr_foreign_toplevel_handle_v1*, wl_array* state);
    static void on_done(void* data, zwlr_foreign_toplevel_handle_v1*);
    static void on_closed(void* data, zwlr_foreign_toplevel_handle_v1*);
    static void on_parent(void* data, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1* parent);

    zwlr_foreign_toplevel_handle_v1* handle_;
    ToplevelInfo info_;
    std::function<void()> on_change_;
    bool closed_ = false;
};

class ToplevelService : public Service {
public:
    ToplevelService(zwlr_foreign_toplevel_manager_v1* manager);
    ~ToplevelService() override;

    bool init() override;
    bool tick() override;

    const std::vector<std::unique_ptr<Toplevel>>& toplevels() const { return toplevels_; }

private:
    static void on_toplevel(void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* handle);
    static void on_finished(void* data, zwlr_foreign_toplevel_manager_v1*);

    zwlr_foreign_toplevel_manager_v1* manager_;
    std::vector<std::unique_ptr<Toplevel>> toplevels_;
    bool dirty_ = false;
};
