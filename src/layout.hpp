#pragma once

#include "widget.hpp"
#include <memory>
#include <vector>

enum class BoxOrientation { Horizontal, Vertical };

class Spacer : public Widget {
public:
    int preferredWidth() const override { return 0; }
    int preferredHeight() const override { return 0; }
    void layout(int x, int y, int w, int h) override { x_ = x; y_ = y; w_ = w; h_ = h; }
    void render(const Renderer&) const override {}
};

class BoxLayout : public Widget {
public:
    BoxLayout(BoxOrientation orient, float gap, float padding);

    void addChild(std::unique_ptr<Widget> child, float flex = 0.0f);
    void addSpacer();

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;
    bool handleClick(int x, int y, uint32_t button) override;
    void handleHover(int x, int y) override;
    void clearHover() override;

private:
    struct Child {
        std::unique_ptr<Widget> widget;
        float flex;
    };

    std::vector<Child> children_;
    BoxOrientation orient_;
    float gap_;
    float padding_;
};
