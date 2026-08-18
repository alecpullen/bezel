#include "config.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <pwd.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace {

std::string configPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/.config/bezel/config.toml";
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return std::string(s.substr(b, e - b));
}

// Parse "0xrrggbb" / "#rrggbb" (with optional surrounding quotes) into a
// 0xRRGGBB uint32. Returns false on parse failure / out-of-range components.
bool parseColor(std::string_view s, uint32_t& out) {
    // strip surrounding quotes if present
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\''))
        s.remove_prefix(1);
    if (s.size() >= 1 && (s.back() == '"' || s.back() == '\''))
        s.remove_suffix(1);

    const char* str = s.data();
    char* end = nullptr;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        str += 2;
    else if (s.size() >= 1 && s[0] == '#')
        str += 1;
    else
        return false;

    unsigned long v = std::strtoul(str, &end, 16);
    if (end == str || *end != '\0') return false;
    if (v > 0xFFFFFFUL) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

} // namespace

std::string SessionConfig::systemUser() const {
    if (!lock_user.empty()) return lock_user;
    const char* env = std::getenv("LOGNAME");
    if (env && *env) return env;
    env = std::getenv("USER");
    if (env && *env) return env;
    struct passwd* pw = getpwuid(geteuid());
    if (pw && pw->pw_name) return pw->pw_name;
    return {};
}

SessionConfig loadSessionConfig() {
    SessionConfig cfg;

    std::ifstream in(configPath());
    if (!in) return cfg;

    bool inSession = false;
    std::string line;
    while (std::getline(in, line)) {
        // Strip inline comment (respecting simple quoted strings, best-effort).
        std::string_view lv(line);
        if (lv.find('#') != std::string_view::npos)
            lv = lv.substr(0, lv.find('#'));
        line = trim(lv);
        if (line.empty()) continue;

        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close == std::string::npos) { inSession = false; continue; }
            std::string header = trim(std::string_view(line).substr(1, close - 1));
            inSession = (header == "session");
            continue;
        }

        if (!inSession) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(std::string_view(line).substr(0, eq));
        std::string val = trim(std::string_view(line).substr(eq + 1));

        if (key == "idle_timeout_sec") {
            char* end = nullptr;
            long v = std::strtol(val.c_str(), &end, 10);
            if (end != val.c_str() && *end == '\0' && v >= 0)
                cfg.idle_timeout_sec = static_cast<int>(v);
        } else if (key == "dim_timeout_sec") {
            char* end = nullptr;
            long v = std::strtol(val.c_str(), &end, 10);
            if (end != val.c_str() && *end == '\0' && v >= 0)
                cfg.dim_timeout_sec = static_cast<int>(v);
        } else if (key == "lock_on_idle") {
            if (val == "true") cfg.lock_on_idle = true;
            else if (val == "false") cfg.lock_on_idle = false;
        } else if (key == "inhibit_idle") {
            if (val == "true") cfg.inhibit_idle = true;
            else if (val == "false") cfg.inhibit_idle = false;
        } else if (key == "lock_bg_color") {
            uint32_t c;
            if (parseColor(val, c)) cfg.lock_bg_color = c;
        } else if (key == "lock_bg_opacity") {
            char* end = nullptr;
            float v = std::strtof(val.c_str(), &end);
            if (end != val.c_str() && *end == '\0' && v >= 0.0f && v <= 1.0f)
                cfg.lock_bg_opacity = v;
        } else if (key == "lock_accent") {
            uint32_t c;
            if (parseColor(val, c)) cfg.lock_accent = c;
        } else if (key == "lock_user") {
            // Strip surrounding quotes.
            if (val.size() >= 2 && (val.front() == '"' || val.front() == '\''))
                val = val.substr(1);
            if (!val.empty() && (val.back() == '"' || val.back() == '\''))
                val.pop_back();
            cfg.lock_user = val;
        }
        // Unknown keys in [session] are ignored.
    }

    return cfg;
}
