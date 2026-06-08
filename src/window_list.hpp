#pragma once
#include "widget.hpp"
#include "toplevel_service.hpp"
#include <functional>
#include <memory>
#include <string>

struct NVGcontext;
class IconLoader;

class WindowList : public Widget {
public:
    WindowList(const Theme& theme, int fontHandle, NVGcontext* ctx,
               wl_output* output, ToplevelService& service);
    ~WindowList() override;

    void setCallback(std::function<void()> cb);

    int  preferredWidth()  const override { return 0; }
    int  preferredHeight() const override { return (int)theme_.tileSizeHorizontal; }
    void layout(int x, int y, int w, int h) override { x_ = x; y_ = y; w_ = w; h_ = h; }
    void render(const Renderer& renderer) const override;

private:
    void drawButton(NVGcontext* vg, float btnX, float btnW,
                    const ToplevelInfo& info) const;
    void drawEllipsized(NVGcontext* vg, float x, float y, float maxW,
                        const std::string& text) const;

    const Theme&                  theme_;
    int                           fontHandle_;
    wl_output*                    output_;
    ToplevelService&              service_;
    std::unique_ptr<IconLoader>   iconLoader_;
    std::function<void()>         callback_;
    int                           subId_ = -1;
};
