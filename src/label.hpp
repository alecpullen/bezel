#pragma once

#include "widget.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include <string>

struct NVGcontext;

class Label : public Widget {
public:
    Label(NVGcontext* ctx, int fontHandle, const std::string& text = "");

    void setText(const std::string& text);
    void setFontSize(float px);
    void setColor(NVGcolor color);
    void setAlign(int align);

    int preferredWidth() const override { return prefW_; }
    int preferredHeight() const override { return prefH_; }
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;

private:
    void recalcPreferred();

    NVGcontext* ctx_;
    int fontHandle_;
    float fontSize_ = 12.0f;
    NVGcolor color_;
    int align_ = 0;
    std::string text_;
    int prefW_ = 0;
    int prefH_ = 0;
};
