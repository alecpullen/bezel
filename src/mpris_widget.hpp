#pragma once
#include "widget.hpp"
#include "mpris_service.hpp"
#include "label.hpp"
#include <memory>
#include <functional>

struct NVGcontext;

class MprisWidget : public Widget {
public:
    MprisWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, MprisService& svc);
    ~MprisWidget() override;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;

    void setCallback(std::function<void()> requestRedraw);

private:
    void rebuildLabels() const;

    const Theme& theme_;
    MprisService& svc_;
    NVGcontext* ctx_;
    int fontHandle_;
    int subId_ = -1;

    mutable std::unique_ptr<Label> titleLabel_;
    mutable std::unique_ptr<Label> artistLabel_;
    mutable MprisInfo lastInfo_;
    mutable int lastArtHandle_ = -1;
    mutable bool labelsDirty_ = true;
    std::function<void()> requestRedraw_;

    static constexpr int ART_SIZE = 20;
    static constexpr int MAX_TEXT_W = 140;
};
