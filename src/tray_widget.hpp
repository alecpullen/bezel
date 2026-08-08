#pragma once
#include "widget.hpp"
#include "tray_service.hpp"
#include "sni_icon_widget.hpp"
#include "theme.hpp"
#include <functional>
#include <memory>
#include <vector>

class TrayWidget : public Widget {
public:
    TrayWidget(const Theme& theme, TrayService& svc, int fontHandle);
    ~TrayWidget() override;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;
    bool handleClick(int x, int y, uint32_t button) override;
    void handleHover(int x, int y) override;
    void clearHover() override;

    void setTooltipCallbacks(std::function<void(int, const std::string&)> show,
                             std::function<void()> dismiss);
    void setCallback(std::function<void()> requestRedraw);

    static constexpr int MAX_VISIBLE = 8;

private:
    void rebuildChildren();

    const Theme& theme_;
    TrayService& svc_;
    int fontHandle_ = -1;
    int subId_ = -1;
    std::vector<std::unique_ptr<SniIconWidget>> icons_;
    bool overflow_ = false;
    bool childrenDirty_ = true;
    std::function<void(int, const std::string&)> showTooltip_;
    std::function<void()> dismissTooltip_;
    std::function<void()> requestRedraw_;
};
