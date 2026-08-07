#pragma once
#include "widget.hpp"
#include "brightness_service.hpp"
#include "label.hpp"
#include <memory>
#include <functional>

struct NVGcontext;

class BrightnessWidget : public Widget {
public:
    BrightnessWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, BrightnessService& svc);
    ~BrightnessWidget() override;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;

    void setCallback(std::function<void()> requestRedraw);

private:
    void updateLabelText() const;

    const Theme& theme_;
    BrightnessService& svc_;
    int subId_ = -1;
    mutable std::unique_ptr<Label> label_;
    mutable BrightnessInfo lastInfo_;
    std::function<void()> requestRedraw_;
};
