#include "icon_loader.hpp"
#include <nanovg.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef HAVE_LIBRSVG
#include <librsvg/rsvg.h>
#include <cairo.h>
#endif

namespace fs = std::filesystem;

IconLoader::IconLoader(NVGcontext* ctx) : ctx_(ctx) {}

IconLoader::~IconLoader() {
    for (auto& [key, handle] : cache_) {
        if (handle >= 0)
            nvgDeleteImage(ctx_, handle);
    }
}

int IconLoader::get(const std::string& app_id) {
    auto it = cache_.find(app_id);
    if (it != cache_.end())
        return it->second;
    int handle = load(app_id);
    cache_[app_id] = handle;
    return handle;
}

static std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

static bool fexists(const std::string& p) {
    return fs::exists(p);
}

int IconLoader::load(const std::string& app_id) {
    if (app_id.empty()) return -1;
    std::string desktop = findDesktopFile(app_id);
    std::string icon_name;
    if (!desktop.empty())
        icon_name = readIconName(desktop);
    if (icon_name.empty())
        icon_name = app_id;
    std::string path = findIconFile(icon_name);
    if (path.empty()) return -1;

    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".svg") == 0)
        return rasterizeSvg(path);

    return nvgCreateImage(ctx_, path.c_str(), 0);
}

std::string IconLoader::findDesktopFile(const std::string& app_id) const {
    std::string home = home_dir();

    std::vector<std::string> names;
    names.push_back(app_id);

    std::string lc = app_id;
    std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
    if (lc != app_id) names.push_back(lc);

    auto dot = app_id.rfind('.');
    if (dot != std::string::npos && dot + 1 < app_id.size()) {
        std::string tail = app_id.substr(dot + 1);
        names.push_back(tail);
        std::string tail_lc = tail;
        std::transform(tail_lc.begin(), tail_lc.end(), tail_lc.begin(), ::tolower);
        if (tail_lc != tail) names.push_back(tail_lc);
    }

    std::vector<std::string> dirs;
    if (!home.empty()) dirs.push_back(home + "/.local/share/applications/");
    dirs.push_back("/usr/share/applications/");

    for (const auto& dir : dirs)
        for (const auto& name : names) {
            std::string p = dir + name + ".desktop";
            if (fexists(p)) return p;
        }

    // Fallback: scan directories for any .desktop file whose StartupWMClass
    // matches app_id (case-insensitive). This fixes apps like JetBrains
    // where the reported app_id does not match the .desktop file name.
    auto read_swm = [](const std::string& path) -> std::string {
        std::ifstream f(path);
        if (!f) return {};
        bool in_entry = false;
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.starts_with("[")) {
                in_entry = (line == "[Desktop Entry]");
                continue;
            }
            if (!in_entry) continue;
            if (line.starts_with("StartupWMClass="))
                return line.substr(15);
        }
        return {};
    };

    std::string app_id_lc = app_id;
    std::transform(app_id_lc.begin(), app_id_lc.end(), app_id_lc.begin(), ::tolower);

    for (const auto& dir : dirs) {
        if (!fs::exists(dir) || !fs::is_directory(dir)) continue;
        try {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                const std::string p = entry.path().string();
                if (!p.ends_with(".desktop")) continue;
                std::string swm = read_swm(p);
                if (swm.empty()) continue;
                std::string swm_lc = swm;
                std::transform(swm_lc.begin(), swm_lc.end(), swm_lc.begin(), ::tolower);
                if (swm_lc == app_id_lc) return p;
            }
        } catch (const fs::filesystem_error&) {
            continue;
        }
    }

    return {};
}

std::string IconLoader::readIconName(const std::string& path) const {
    std::ifstream f(path);
    if (!f) return {};

    bool in_entry = false;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            in_entry = (line == "[Desktop Entry]");
            continue;
        }
        if (!in_entry) continue;
        if (line.rfind("Icon=", 0) == 0)
            return line.substr(5);
    }
    return {};
}

std::string IconLoader::findIconFile(const std::string& name) const {
    if (name.empty()) return {};

    if (name[0] == '/') {
        if (fexists(name)) return name;
        std::string with_png = name + ".png";
        if (fexists(with_png)) return with_png;
        return {};
    }

    std::string home = home_dir();

    // Prefer larger PNGs first (we scale down to 16px display size so quality matters)
    static constexpr const char* kSizes[] = {
        "512x512", "256x256", "192x192", "128x128", "96x96",
        "64x64", "48x48", "32x32", nullptr
    };

    std::vector<std::string> bases;
    if (!home.empty()) {
        bases.push_back(home + "/.local/share/icons/hicolor/");
        bases.push_back(home + "/.icons/hicolor/");
    }
    bases.push_back("/usr/share/icons/hicolor/");

    for (const auto& base : bases)
        for (int i = 0; kSizes[i]; ++i) {
            std::string p = base + kSizes[i] + "/apps/" + name + ".png";
            if (fexists(p)) return p;
        }

    // SVG fallback: scalable/apps/ in hicolor dirs
    for (const auto& base : bases) {
        std::string p = base + "scalable/apps/" + name + ".svg";
        if (fexists(p)) return p;
    }

    // Pixmaps: PNG then SVG
    {
        std::string p = "/usr/share/pixmaps/" + name + ".png";
        if (fexists(p)) return p;
    }
    {
        std::string p = "/usr/share/pixmaps/" + name + ".svg";
        if (fexists(p)) return p;
    }

    return {};
}

int IconLoader::rasterizeSvg(const std::string& path) const {
#ifdef HAVE_LIBRSVG
    static constexpr int SIZE = 64;

    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
    if (!handle) {
        if (err) g_error_free(err);
        return -1;
    }

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SIZE, SIZE);
    cairo_t* cr = cairo_create(surf);

    RsvgRectangle vp = { 0.0, 0.0, (double)SIZE, (double)SIZE };
    rsvg_handle_render_document(handle, cr, &vp, &err);
    if (err) g_error_free(err);

    cairo_destroy(cr);
    g_object_unref(handle);

    cairo_surface_flush(surf);
    const unsigned char* src = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);

    // Cairo ARGB32 (premultiplied, little-endian bytes: B G R A) → NanoVG RGBA straight alpha
    std::vector<unsigned char> rgba(SIZE * SIZE * 4);
    for (int y = 0; y < SIZE; y++) {
        const unsigned char* row = src + y * stride;
        for (int x = 0; x < SIZE; x++) {
            unsigned char b = row[x*4 + 0];
            unsigned char g = row[x*4 + 1];
            unsigned char r = row[x*4 + 2];
            unsigned char a = row[x*4 + 3];
            if (a > 0) {
                r = (unsigned char)(std::min(255, (int)r * 255 / a));
                g = (unsigned char)(std::min(255, (int)g * 255 / a));
                b = (unsigned char)(std::min(255, (int)b * 255 / a));
            }
            rgba[(y * SIZE + x) * 4 + 0] = r;
            rgba[(y * SIZE + x) * 4 + 1] = g;
            rgba[(y * SIZE + x) * 4 + 2] = b;
            rgba[(y * SIZE + x) * 4 + 3] = a;
        }
    }

    cairo_surface_destroy(surf);
    return nvgCreateImageRGBA(ctx_, SIZE, SIZE, 0, rgba.data());
#else
    (void)path;
    return -1;
#endif
}
