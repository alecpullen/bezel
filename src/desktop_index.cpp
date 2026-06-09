#include "desktop_index.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Split a semicolon-delimited list (e.g. "GNOME;KDE;") into tokens.
static std::vector<std::string> split_semicolon(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ';'))
        if (!tok.empty()) out.push_back(tok);
    return out;
}

std::string DesktopIndex::stripExecCodes(const std::string& exec,
                                          const std::string& icon,
                                          const std::string& name,
                                          const std::string& path) {
    std::string out;
    out.reserve(exec.size());
    for (size_t i = 0; i < exec.size(); ++i) {
        if (exec[i] != '%' || i + 1 >= exec.size()) {
            out += exec[i];
            continue;
        }
        char code = exec[i + 1];
        ++i;
        switch (code) {
            case 'f': case 'F': case 'u': case 'U':
            case 'd': case 'D': case 'n': case 'N':
            case 'v': case 'm':
                // file/URL placeholders — omit (provided at launch time)
                break;
            case 'i':
                if (!icon.empty()) { out += "--icon "; out += icon; }
                break;
            case 'c':
                out += name;
                break;
            case 'k':
                out += path;
                break;
            case '%':
                out += '%';
                break;
            default:
                break; // unknown code — silently drop
        }
    }
    // Trim leading/trailing whitespace
    size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos) return {};
    size_t end = out.find_last_not_of(' ');
    return out.substr(start, end - start + 1);
}

static bool parse_bool(const std::string& v) {
    return v == "true" || v == "True" || v == "TRUE" || v == "1";
}

// Try to match against $XDG_CURRENT_DESKTOP (semicolon-delimited list).
// Returns true if the filter says to SKIP this entry.
static bool filtered_out(const std::string& only_show_in, const std::string& not_show_in) {
    const char* de_env = std::getenv("XDG_CURRENT_DESKTOP");
    if (!de_env || *de_env == '\0') return false; // unknown DE — show everything

    std::string de = to_lower(de_env);

    if (!only_show_in.empty()) {
        for (const auto& allowed : split_semicolon(only_show_in))
            if (to_lower(allowed) == de) return false;
        return true; // not in the only-show list
    }
    if (!not_show_in.empty()) {
        for (const auto& blocked : split_semicolon(not_show_in))
            if (to_lower(blocked) == de) return true;
    }
    return false;
}

void DesktopIndex::scan() {
    entries_.clear();
    index_.clear();

    std::vector<std::string> dirs;
    std::string home = home_dir();
    if (!home.empty()) dirs.push_back(home + "/.local/share/applications/");
    dirs.push_back("/usr/share/applications/");

    for (const auto& dir : dirs) {
        if (!fs::exists(dir) || !fs::is_directory(dir)) continue;

        try {
            for (const auto& dent : fs::directory_iterator(dir)) {
                if (!dent.is_regular_file()) continue;
                const std::string p = dent.path().string();
                if (!p.ends_with(".desktop")) continue;

                std::ifstream f(p);
                if (!f) continue;

                // Per-entry field accumulator
                std::string name, generic_name, comment, exec, icon, swm;
                std::string only_show_in, not_show_in;
                bool no_display = false, hidden = false;
                bool in_entry = false, done = false;

                std::string line;
                while (!done && std::getline(f, line)) {
                    // Strip trailing \r and spaces
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                        line.pop_back();
                    if (line.empty() || line[0] == '#') continue;

                    if (line[0] == '[') {
                        if (in_entry) { done = true; break; } // left [Desktop Entry]
                        in_entry = (line == "[Desktop Entry]");
                        continue;
                    }
                    if (!in_entry) continue;

                    auto eq = line.find('=');
                    if (eq == std::string::npos) continue;
                    std::string key = line.substr(0, eq);
                    std::string val = line.substr(eq + 1);

                    if      (key == "Name")             name          = val;
                    else if (key == "GenericName")      generic_name  = val;
                    else if (key == "Comment")          comment       = val;
                    else if (key == "Exec")             exec          = val;
                    else if (key == "Icon")             icon          = val;
                    else if (key == "StartupWMClass")   swm           = val;
                    else if (key == "NoDisplay")        no_display    = parse_bool(val);
                    else if (key == "Hidden")           hidden        = parse_bool(val);
                    else if (key == "OnlyShowIn")       only_show_in  = val;
                    else if (key == "NotShowIn")        not_show_in   = val;
                }

                if (!in_entry && !done) continue;   // no [Desktop Entry] section
                if (name.empty()) continue;          // not a real app entry
                if (no_display || hidden) continue;
                if (filtered_out(only_show_in, not_show_in)) continue;

                DesktopEntry entry;
                entry.name             = name;
                entry.generic_name     = generic_name;
                entry.comment          = comment;
                entry.icon             = icon;
                entry.startup_wm_class = swm;
                entry.desktop_path     = p;
                entry.desktop_id       = dent.path().stem().string();
                entry.exec             = stripExecCodes(exec, icon, name, p);

                size_t idx = entries_.size();
                entries_.push_back(std::move(entry));

                const DesktopEntry& e = entries_.back();

                // Index by desktop_id variants (try_emplace so user-local wins)
                index_.try_emplace(e.desktop_id, idx);
                index_.try_emplace(to_lower(e.desktop_id), idx);

                auto dot = e.desktop_id.rfind('.');
                if (dot != std::string::npos && dot + 1 < e.desktop_id.size()) {
                    std::string tail = e.desktop_id.substr(dot + 1);
                    index_.try_emplace(tail, idx);
                    index_.try_emplace(to_lower(tail), idx);
                }

                if (!e.startup_wm_class.empty())
                    index_.try_emplace(e.startup_wm_class, idx);
            }
        } catch (const fs::filesystem_error&) {
            continue;
        }
    }
}

const DesktopEntry* DesktopIndex::findByAppId(const std::string& app_id) const {
    auto probe = [&](const std::string& key) -> const DesktopEntry* {
        auto it = index_.find(key);
        return it != index_.end() ? &entries_[it->second] : nullptr;
    };

    if (const auto* e = probe(app_id))             return e;
    if (const auto* e = probe(to_lower(app_id)))   return e;

    auto dot = app_id.rfind('.');
    if (dot != std::string::npos && dot + 1 < app_id.size()) {
        std::string tail = app_id.substr(dot + 1);
        if (const auto* e = probe(tail))            return e;
        if (const auto* e = probe(to_lower(tail)))  return e;
    }

    return nullptr;
}
