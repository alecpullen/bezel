#pragma once
#include "widget.hpp"
#include "toplevel_service.hpp"
#include <functional>
#include <memory>
#include <set>
#include <string>

struct NVGcontext;
class IconLoader;

class WindowList : public Widget {
public:
    struct ContextMenuRequest {
        zwlr_foreign_toplevel_handle_v1* handle;
        std::string app_id;
        int anchorX;
    };

    WindowList(const Theme& theme, int fontHandle, NVGcontext* ctx,
               wl_output* output, ToplevelService& service, int panelHeight);
    ~WindowList() override;

    void setCallback(std::function<void()> cb);
    void setSeat(wl_seat* seat) { seat_ = seat; }
    void setPinnedAppIds(const std::set<std::string>& ids);
    void setContextMenuCallback(std::function<void(ContextMenuRequest)> cb);

    int  preferredWidth()  const override;
    int  preferredHeight() const override { return (int)theme_.tileSizeHorizontal; }
    void layout(int x, int y, int w, int h) override { x_ = x; y_ = y; w_ = w; h_ = h; }
    void render(const Renderer& renderer) const override;
    bool handleClick(int x, int y, uint32_t button) override;
    void handleHover(int x, int y) override;
    void clearHover() override;

private:
    struct ButtonRegion {
        zwlr_foreign_toplevel_handle_v1* handle;
        std::string app_id;
        int x, w;
    };

    std::vector<const ToplevelInfo*> visibleToplevels() const;
    void drawButton(NVGcontext* vg, float btnX, float btnW,
                    const ToplevelInfo& info, bool hovered) const;
    void drawGhostButton(NVGcontext* vg, float btnX, float btnW,
                         const std::string& app_id, bool hovered) const;
    void drawEllipsized(NVGcontext* vg, float x, float y, float maxW,
                        const std::string& text) const;

    const Theme&                  theme_;
    int                           fontHandle_;
    wl_output*                    output_;
    ToplevelService&              service_;
    int                           panelHeight_;
    std::unique_ptr<IconLoader>   iconLoader_;
    std::function<void()>         callback_;
    std::function<void(ContextMenuRequest)> contextMenuCb_;
    int                           subId_ = -1;
    wl_seat*                      seat_  = nullptr;
    std::set<std::string>         pinnedAppIds_;
    mutable std::vector<ButtonRegion> buttonRegions_;
    int hoverX_ = -1;
};
