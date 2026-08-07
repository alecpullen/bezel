#pragma once
#include "widget.hpp"
#include "network_service.hpp"
#include "label.hpp"
#include <memory>
#include <functional>

struct NVGcontext;

class NetworkWidget : public Widget {
public:
    NetworkWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, NetworkService& svc);
    ~NetworkWidget() override;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;

    void setCallback(std::function<void()> requestRedraw);

private:
    void updateLabelText() const;

    const Theme& theme_;
    NetworkService& svc_;
    int subId_ = -1;
    mutable std::unique_ptr<Label> label_;
    mutable NetworkInfo lastInfo_;
    std::function<void()> requestRedraw_;
};
