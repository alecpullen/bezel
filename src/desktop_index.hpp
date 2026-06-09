#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct DesktopEntry {
    std::string name;
    std::string generic_name;
    std::string comment;
    std::string exec;             // Exec= with %x field codes stripped
    std::string icon;
    std::string startup_wm_class;
    std::string desktop_id;       // filename stem, e.g. "org.gnome.Nautilus"
    std::string desktop_path;     // absolute path to the .desktop file
};

class DesktopIndex {
public:
    void scan();

    const std::vector<DesktopEntry>& entries() const { return entries_; }
    const DesktopEntry* findByAppId(const std::string& app_id) const;

private:
    static std::string stripExecCodes(const std::string& exec, const std::string& icon,
                                      const std::string& name, const std::string& path);

    std::vector<DesktopEntry> entries_;
    std::unordered_map<std::string, size_t> index_;
};
