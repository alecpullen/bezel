#pragma once
#include "widget.hpp"
#include "audio_service.hpp"
#include "label.hpp"
#include <memory>
#include <functional>

struct NVGcontext;

class VolumeWidget : public Widget {
public:
    VolumeWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, AudioService& svc);
    ~VolumeWidget() override;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;

    void setCallback(std::function<void()> requestRedraw);

private:
    void updateLabelText() const;

    const Theme& theme_;
    AudioService& svc_;
    int subId_ = -1;
    mutable std::unique_ptr<Label> label_;
    mutable AudioInfo lastInfo_;
    std::function<void()> requestRedraw_;
};
