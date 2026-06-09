// layout.cpp
#include "layout.hpp"

#include <algorithm>

BoxLayout::BoxLayout(BoxOrientation orient, float gap, float padding)
    : orient_(orient), gap_(gap), padding_(padding) {}

void BoxLayout::addChild(std::unique_ptr<Widget> child, float flex) {
    children_.push_back({std::move(child), flex});
}

void BoxLayout::addSpacer() {
    addChild(std::make_unique<Spacer>(), 1.0f);
}

int BoxLayout::preferredWidth() const {
    if (orient_ == BoxOrientation::Horizontal) {
        int total = static_cast<int>(padding_ * 2);
        for (const auto& c : children_)
            total += c.widget->preferredWidth();
        if (!children_.empty()) total += static_cast<int>(gap_ * (children_.size() - 1));
        return total;
    } else {
        int maxW = 0;
        for (const auto& c : children_)
            maxW = std::max(maxW, c.widget->preferredWidth());
        return maxW + static_cast<int>(padding_ * 2);
    }
}

int BoxLayout::preferredHeight() const {
    if (orient_ == BoxOrientation::Vertical) {
        int total = static_cast<int>(padding_ * 2);
        for (const auto& c : children_)
            total += c.widget->preferredHeight();
        if (!children_.empty()) total += static_cast<int>(gap_ * (children_.size() - 1));
        return total;
    } else {
        int maxH = 0;
        for (const auto& c : children_)
            maxH = std::max(maxH, c.widget->preferredHeight());
        return maxH + static_cast<int>(padding_ * 2);
    }
}

void BoxLayout::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;

    int innerW = w - static_cast<int>(padding_ * 2);
    int innerH = h - static_cast<int>(padding_ * 2);
    int mainSize = (orient_ == BoxOrientation::Horizontal) ? innerW : innerH;
    int crossSize = (orient_ == BoxOrientation::Horizontal) ? innerH : innerW;

    float totalFlex = 0.0f;
    int fixedSum = 0;
    for (const auto& c : children_) {
        if (c.flex > 0.0f)
            totalFlex += c.flex;
        else {
            fixedSum += (orient_ == BoxOrientation::Horizontal)
                ? c.widget->preferredWidth() : c.widget->preferredHeight();
        }
    }

    int remaining = mainSize - fixedSum;
    if (!children_.empty()) remaining -= static_cast<int>(gap_ * (children_.size() - 1));
    if (remaining < 0) remaining = 0;

    int pos = 0;
    for (const auto& c : children_) {
        int childMain, childCross;
        if (c.flex > 0.0f && totalFlex > 0.0f) {
            childMain = static_cast<int>(remaining * c.flex / totalFlex);
        } else {
            childMain = (orient_ == BoxOrientation::Horizontal)
                ? c.widget->preferredWidth() : c.widget->preferredHeight();
        }

        childCross = (orient_ == BoxOrientation::Horizontal)
            ? std::min(c.widget->preferredHeight(), crossSize) : std::min(c.widget->preferredWidth(), crossSize);

        int cx = (orient_ == BoxOrientation::Horizontal) ? pos : 0;
        int cy = (orient_ == BoxOrientation::Horizontal) ? 0 : pos;

        c.widget->layout(
            static_cast<int>(x + padding_) + cx,
            static_cast<int>(y + padding_) + cy,
            (orient_ == BoxOrientation::Horizontal) ? childMain : childCross,
            (orient_ == BoxOrientation::Horizontal) ? childCross : childMain);

        pos += childMain + static_cast<int>(gap_);
    }
}

void BoxLayout::render(const Renderer& renderer) const {
    for (const auto& c : children_)
        c.widget->render(renderer);
}

bool BoxLayout::handleClick(int x, int y, uint32_t button) {
    for (const auto& c : children_) {
        Widget* w = c.widget.get();
        if (x >= w->x() && x < w->x() + w->width() &&
            y >= w->y() && y < w->y() + w->height())
            if (w->handleClick(x, y, button)) return true;
    }
    return false;
}

void BoxLayout::handleHover(int x, int y) {
    for (const auto& c : children_) {
        Widget* w = c.widget.get();
        if (x >= w->x() && x < w->x() + w->width() &&
            y >= w->y() && y < w->y() + w->height())
            w->handleHover(x, y);
        else
            w->clearHover();
    }
}

void BoxLayout::clearHover() {
    for (const auto& c : children_)
        c.widget->clearHover();
}
