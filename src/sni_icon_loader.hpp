#pragma once
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

struct NVGcontext;

class SniIconLoader {
public:
    explicit SniIconLoader(NVGcontext* vg);
    ~SniIconLoader();

    SniIconLoader(const SniIconLoader&) = delete;
    SniIconLoader& operator=(const SniIconLoader&) = delete;

    void setNvgContext(NVGcontext* vg) { vg_ = vg; }

    // pixmaps is the SNI IconPixmap: array of (width, height, ARGB32 bytes).
    // Returns NanoVG image handle or -1 on failure.
    int fromPixmap(const std::vector<std::tuple<int32_t, int32_t, std::vector<uint8_t>>>& pixmaps);

    // Resolve an icon name via themePath + hicolor dirs. Returns handle or -1.
    int fromIconName(const std::string& name, const std::string& themePath);

    void destroy(int handle);

private:
    int rasterizeSvg(const std::string& path) const;
    NVGcontext* vg_;
};
