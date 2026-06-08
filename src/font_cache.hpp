#pragma once

#include "theme.hpp"
#include <string>
#include <string_view>
#include <vector>

struct NVGcontext;

class FontCache {
public:
    explicit FontCache(NVGcontext* ctx, std::string_view fontDir = "/usr/share/fonts");

    int loadSans();
    int loadMono();
    int uiSans() const { return uiSans_; }

private:
    int load(std::string_view name, std::string_view relPath);

    NVGcontext* ctx_;
    std::string fontDir_;
    int uiSans_ = 0;
};
