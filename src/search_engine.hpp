#pragma once
#include <string>
#include <string_view>
#include <vector>

struct zwlr_foreign_toplevel_handle_v1;
class DesktopIndex;
class ToplevelService;

enum class ResultKind : int { Application = 0, Window = 1, Command = 2 };
enum class CommandAction {
    Lock, Suspend, Logout, QuitBezel,
    Reboot, PowerOff, Hibernate,
};

struct SearchResult {
    ResultKind   kind;
    std::string  name;
    std::string  subtitle;        // type label shown at 11px muted
    std::string  icon_name;       // passed to IconLoader::get(); empty = no icon
    std::string  exec;            // for app launch (posix_spawn)
    std::string  app_id;
    zwlr_foreign_toplevel_handle_v1* toplevel_handle = nullptr;
    CommandAction command_action = CommandAction::Lock;
    int          score = 0;
};

class SearchEngine {
public:
    // toplevels may be null — window results are skipped gracefully
    SearchEngine(const DesktopIndex& desktop, const ToplevelService* toplevels);

    // Returns results sorted by (kind asc, score desc). Empty query → empty list.
    // Prefix '>' or '.' restricts scope to commands only (prefix stripped before matching).
    std::vector<SearchResult> query(std::string_view q) const;

private:
    const DesktopIndex&    desktop_;
    const ToplevelService* toplevels_;
};
