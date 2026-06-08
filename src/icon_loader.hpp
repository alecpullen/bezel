#pragma once
#include <string>
#include <unordered_map>

struct NVGcontext;

class IconLoader {
public:
    explicit IconLoader(NVGcontext* ctx);
    ~IconLoader();

    // Returns a NVG image handle for app_id, or -1 if not found. Result is cached.
    int get(const std::string& app_id);

private:
    int         load(const std::string& app_id);
    std::string findDesktopFile(const std::string& app_id) const;
    std::string readIconName(const std::string& desktop_path) const;
    std::string findIconFile(const std::string& icon_name) const;
    int         rasterizeSvg(const std::string& path) const;

    NVGcontext* ctx_;
    std::unordered_map<std::string, int> cache_;
};
