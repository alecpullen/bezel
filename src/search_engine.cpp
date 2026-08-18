#include "search_engine.hpp"
#include "desktop_index.hpp"
#include "toplevel_service.hpp"
#include <algorithm>
#include <cctype>
#include <string>

namespace {

// Subsequence fuzzy match. Returns false if query chars aren't all found in order.
// score accumulates: +10 per contiguous run, +5 per word-boundary start.
bool fuzzyMatch(std::string_view query, std::string_view candidate, int& score) {
    std::string q_lower(query.size(), '\0'), c_lower(candidate.size(), '\0');
    std::transform(query.begin(),    query.end(),    q_lower.begin(), ::tolower);
    std::transform(candidate.begin(), candidate.end(), c_lower.begin(), ::tolower);

    score = 0;
    int    prev_pos = -1;
    size_t c_idx    = 0;

    for (char qc : q_lower) {
        bool found = false;
        for (; c_idx < c_lower.size(); ++c_idx) {
            if (c_lower[c_idx] == qc) {
                int pos = static_cast<int>(c_idx);
                if (prev_pos >= 0 && pos == prev_pos + 1) score += 10;
                if (pos == 0 || c_lower[pos - 1] == ' ' || c_lower[pos - 1] == '-' ||
                    c_lower[pos - 1] == '_' || c_lower[pos - 1] == '.')
                    score += 5;
                prev_pos = pos;
                ++c_idx;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

struct CommandEntry {
    const char*   name;
    const char*   icon_name;
    CommandAction action;
};

constexpr CommandEntry kCommands[] = {
    {"Lock",       "system-lock-screen", CommandAction::Lock},
    {"Log Out",    "system-log-out",     CommandAction::Logout},
    {"Suspend",    "system-suspend",     CommandAction::Suspend},
    {"Hibernate",  "system-hibernate",   CommandAction::Hibernate},
    {"Reboot",     "system-reboot",      CommandAction::Reboot},
    {"Shut Down",  "system-shutdown",    CommandAction::PowerOff},
    {"Quit Bezel", "",                   CommandAction::QuitBezel},
};

} // namespace

SearchEngine::SearchEngine(const DesktopIndex& desktop, const ToplevelService* toplevels)
    : desktop_(desktop), toplevels_(toplevels) {}

std::vector<SearchResult> SearchEngine::query(std::string_view q) const {
    if (q.empty()) return {};

    bool commands_only = (q[0] == '>' || q[0] == '.');
    std::string_view eff = commands_only ? q.substr(1) : q;
    if (eff.empty()) return {};

    std::vector<SearchResult> results;

    if (!commands_only) {
        for (const auto& entry : desktop_.entries()) {
            int score = 0;
            if (fuzzyMatch(eff, entry.name, score)) {
                results.push_back({
                    .kind            = ResultKind::Application,
                    .name            = entry.name,
                    .subtitle        = "Application",
                    .icon_name       = entry.icon,
                    .exec            = entry.exec,
                    .app_id          = entry.desktop_id,
                    .toplevel_handle = nullptr,
                    .command_action  = CommandAction::Lock,
                    .score           = score,
                });
            }
        }

        if (toplevels_) {
            for (const auto& t : toplevels_->toplevels()) {
                if (t->is_closed() || t->info().activated) continue;
                int score = 0;
                if (fuzzyMatch(eff, t->info().title, score)) {
                    results.push_back({
                        .kind            = ResultKind::Window,
                        .name            = t->info().title,
                        .subtitle        = "Window",
                        .icon_name       = t->info().app_id,
                        .exec            = {},
                        .app_id          = t->info().app_id,
                        .toplevel_handle = t->handle(),
                        .command_action  = CommandAction::Lock,
                        .score           = score,
                    });
                }
            }
        }
    }

    for (const auto& cmd : kCommands) {
        int score = 0;
        if (fuzzyMatch(eff, cmd.name, score)) {
            results.push_back({
                .kind            = ResultKind::Command,
                .name            = cmd.name,
                .subtitle        = "System",
                .icon_name       = cmd.icon_name,
                .exec            = {},
                .app_id          = {},
                .toplevel_handle = nullptr,
                .command_action  = cmd.action,
                .score           = score,
            });
        }
    }

    std::stable_sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.score > b.score;
    });

    return results;
}
