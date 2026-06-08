// font_cache.cpp
#include "font_cache.hpp"

#include <nanovg.h>
#include <cstdio>

namespace {

bool fileExists(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

}

FontCache::FontCache(NVGcontext* ctx, std::string_view fontDir)
    : ctx_(ctx), fontDir_(fontDir) {}

int FontCache::load(std::string_view name, std::string_view relPath) {
    std::string full = std::string(fontDir_) + "/" + std::string(relPath);
    int id = nvgCreateFont(ctx_, std::string(name).c_str(), full.c_str());
    if (id < 0) {
        std::fprintf(stderr, "FontCache: failed to load font '%s' from '%s'\n",
                     std::string(name).c_str(), full.c_str());
    }
    return id;
}

int FontCache::loadSans() {
    struct Candidate { const char* name; const char* path; };
    static const Candidate candidates[] = {
        {"ui-sans-dejavu",  "TTF/DejaVuSans.ttf"},
        {"ui-sans-adwaita", "Adwaita/AdwaitaSans-Regular.ttf"},
    };

    for (const auto& c : candidates) {
        std::string full = std::string(fontDir_) + "/" + c.path;
        if (fileExists(full.c_str())) {
            int id = load(c.name, c.path);
            if (id >= 0) {
                uiSans_ = id;
                return id;
            }
        }
    }

    std::fprintf(stderr, "FontCache: no system sans found in '%s'\n", std::string(fontDir_).c_str());
    return -1;
}

int FontCache::loadMono() {
    static const char* candidates[] = {
        "TTF/DejaVuSansMono.ttf",
        "Adwaita/AdwaitaMono-Regular.ttf",
    };

    for (const char* rel : candidates) {
        std::string full = std::string(fontDir_) + "/" + rel;
        if (fileExists(full.c_str())) {
            int id = load("ui-mono", rel);
            if (id >= 0) return id;
        }
    }

    std::fprintf(stderr, "FontCache: no mono font found, falling back to sans for mono\n");
    return uiSans_;
}
