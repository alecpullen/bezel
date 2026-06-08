#pragma once
#include "widget.hpp"
#include "battery_service.hpp"
#include "label.hpp"
#include <memory>
#include <functional>

struct NVGcontext;

class BatteryWidget : public Widget {
public:
    BatteryWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, BatteryService& svc);
    ~BatteryWidget() override;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;

    void setCallback(std::function<void()> requestRedraw);

private:
    void updateLabelText() const;

    const Theme& theme_;
    BatteryService& svc_;
    int subId_ = -1;

    mutable std::unique_ptr<Label> percentLabel_;
    mutable BatteryInfo lastInfo_;
    std::function<void()> requestRedraw_;
};
