#pragma once
#include "widget.hpp"
#include "label.hpp"
#include <functional>
#include <memory>

struct NVGcontext;

// Panel button that opens the power menu on click.
class PowerButtonWidget : public Widget {
public:
    PowerButtonWidget(const Theme& theme, NVGcontext* ctx, int fontHandle);
    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;
    bool handleClick(int x, int y, uint32_t button) override;
    void handleHover(int x, int y) override;
    void clearHover() override;

    void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }
    void setCallback(std::function<void()> cb) { requestRedraw_ = std::move(cb); }

private:
    const Theme& theme_;
    std::unique_ptr<Label> label_;
    bool hovered_ = false;
    std::function<void()> onClick_;
    std::function<void()> requestRedraw_;
};
