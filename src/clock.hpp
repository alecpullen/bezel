#pragma once

#include "widget.hpp"
#include "theme.hpp"
#include "label.hpp"
#include <chrono>
#include <memory>
#include <string>

struct NVGcontext;

class Clock : public Widget {
public:
    Clock(const Theme& theme, NVGcontext* ctx, int fontHandle);
    ~Clock() override = default;

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const class Renderer& renderer) const override;

    void setDirty();
    bool tick() const;

private:
    const Theme& theme_;

    mutable std::unique_ptr<Label> timeLabel_;
    mutable std::unique_ptr<Label> dateLabel_;
    mutable std::chrono::minutes lastMinute_;
    mutable std::string cachedTime_;
    mutable std::string cachedDate_;
};
