#pragma once

#include "renderer.hpp"
#include "theme.hpp"
#include <cstdint>

struct NVGcontext;

class Widget {
public:
    virtual ~Widget() = default;
    virtual int preferredWidth() const = 0;
    virtual int preferredHeight() const = 0;
    virtual void layout(int x, int y, int w, int h) = 0;
    virtual void render(const Renderer& renderer) const = 0;
    virtual bool hit_test(int, int) const { return false; }

    int width() const { return w_; }
    int height() const { return h_; }
    int x() const { return x_; }
    int y() const { return y_; }

protected:
    int x_ = 0;
    int y_ = 0;
    int w_ = 0;
    int h_ = 0;
};
