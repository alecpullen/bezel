#pragma once
#include "widget.hpp"
#include "sni_item.hpp"
#include "theme.hpp"
#include <functional>

struct NVGcontext;

class SniIconWidget : public Widget {
public:
    SniIconWidget(const Theme& theme, SniItem& item, int fontHandle);
    ~SniIconWidget() override = default;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;
    bool handleClick(int x, int y, uint32_t button) override;
    void handleHover(int x, int y) override;
    void clearHover() override;

    void setTooltipCallbacks(std::function<void(int, const std::string&)> show,
                             std::function<void()> dismiss);

private:
    const Theme& theme_;
    SniItem& item_;
    int fontHandle_ = -1;
    bool hovered_ = false;
    std::function<void(int, const std::string&)> showTooltip_;
    std::function<void()> dismissTooltip_;
};
