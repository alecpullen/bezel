# bezel — UI/UX design spec

**Version:** 0.1
**Date:** 2026-06-08
**Status:** Design 1 active (build target), Design 2 deferred
**Scope:** Visual and interaction design for the shell chrome. Implementation architecture (single-process, components-over-services) is covered separately; this document is the design reference those components render against.

---

## 1. Design philosophy

- **Committed panels only.** Every shell surface owns ~90%+ of the edge it occupies. No floating islands, no short/centred docks. A panel is anchored edge-to-edge with an exclusive zone.
- **One surface does multiple jobs.** Rather than spawning separate launcher/switcher overlays, a panel changes *mode* in place. Fewer surfaces, one exclusive zone, one design system.
- **One process, one theme.** All panels are components of a single process sharing one set of services and one token set. Cohesion ("tight") comes from single sources of truth, not from matching configs across separate binaries.
- **Tiling-aware.** The shell reflects the compositor's actual window layout, not just a flat icon list. Built for a wlroots/dwl-style tiling compositor (MangoWC).
- **Calm but capable.** Default density is moderate; richer states (command mode, expanded vertical) are summoned, not always-on.
- **Layout is configuration, not architecture.** Horizontal and vertical layouts are alternative component sets over the same services. Choosing one is a config toggle, reversible at any time.

---

## 2. Global design tokens

Default theme: **Nocturne** (Tokyo Night-derived). Themeable later; these are the starting values.

### 2.1 Colour

| Token | Value | Use |
|---|---|---|
| `panel.bg` | `#16161e` | Panel surface |
| `panel.bg.elevated` | `rgba(192,202,245,0.07)` | Hover / running-app tile fill |
| `desktop.bg` | `#1a1b26` | Window-chrome backdrop / reference wallpaper |
| `text.primary` | `#c0caf5` | Primary labels, active items |
| `text.secondary` | `#a9b1d6` | Inactive labels, tray icons |
| `text.muted` | `#565f89` | Subtitles, dates, placeholders |
| `accent` | `#7aa2f7` | Active indicators, selection, launcher |
| `accent.tint` | `rgba(122,162,247,0.16)` | Active/selected fills |
| `border.hairline` | `rgba(192,202,245,0.10)` | Default separators |
| `border.emphasis` | `rgba(192,202,245,0.14)` | Window/panel edges |
| `border.accent` | `rgba(122,162,247,0.40)` | Command-mode / focus edge |

Status accents (for syntax, app glyphs, semantic states): green `#9ece6a`, cyan `#7dcfff`, magenta `#bb9af7`, yellow `#e0af68`, red `#f7768e`.

### 2.2 Typography

- UI font: sans (system sans / Inter-class). Mono font: for terminal/code surfaces only.
- Sizes: primary label `12px`, secondary/subtitle `11px`, large clock `22px`. Never below `11px`.
- Weights: `400` regular, `500` for active labels and the large clock. Two weights only.

### 2.3 Geometry

| Token | Value |
|---|---|
| `radius.tile` | 8px |
| `radius.control` | 9–10px (search field, panel-internal) |
| `radius.window` | 10px |
| `tile.size.horizontal` | 34px |
| `tile.size.vertical` | 40px |
| `workspace.map` | 36 × 26px |
| `panel.pad` | 10–12px |
| `gap.item` | 6–10px |
| `icon.inline` | 16–20px |

---

## 3. Design 1 — unified horizontal panel (BUILD TARGET)

A single full-width committed panel that is simultaneously the taskbar, the application launcher, and the workspace switcher. Default anchor **bottom**; top/bottom is a config option (build bottom first).

### 3.1 Default mode

Height **48px**. Exclusive zone = 48px. Left-to-right regions:

1. **Launcher / command trigger** — accent tile (search glyph). Click, or `Super`, enters command mode.
2. **Workspace switcher** — see toggle below. Default renders **tiling maps**: each workspace is a 36×26 box containing proportional rectangles mirroring that workspace's live window layout. Active workspace gets an accent border + brighter tiles. Click a tile to focus a workspace; (stretch) click a sub-rectangle to focus that window.
3. **Window list** — labelled buttons (icon + title), Windows-style. Active window has an elevated fill + a 2px accent underline. Sourced from `wlr-foreign-toplevel`.
4. **Spacer.**
5. **System tray** — status glyphs (network, volume, battery; later StatusNotifierItem items).
6. **Clock** — time (12px primary) over date (11px muted), right-aligned.

### 3.2 Command mode (the "multi-job" idea)

Triggered from the launcher tile or `Super`. The **same surface** stays anchored; it grows to **62px** and swaps content:

- A query field takes the left (focus-grabbed; panel sets `keyboard_interactivity`).
- Results render as a single ranked horizontal list mixing all scopes — **applications**, **open windows**, and **actions/commands** — each with an icon, name (12px), and a type subtitle (11px muted). First result pre-selected (accent fill).
- Ranking order within the list: applications first, open windows second, actions/commands third.
- The currently-focused window is excluded from results while it holds focus. Once focus moves to another window, the previously-focused window re-enters the pool immediately — no cooldown.
- Enter activates the selection; `Esc` collapses back to default mode.
- The launcher is therefore *not* a separate component — it is a mode of this panel.

### 3.3 Toggleable options

- **Workspace render mode:** `tiling-maps` (default) | `flat-icons` (classic numbered/icon pips). The tiling map needs live per-window geometry from the compositor IPC; flat-icons needs only the workspace list, so flat is the graceful fallback when geometry isn't available.
- **Anchor:** `bottom` (default) | `top`.
- **Window-button labels:** `on` (default) | `icons-only` (compact).

### 3.4 Behaviour notes

- Running/active indicators: active window underline (accent); workspace activity via populated tiles.
- Multi-monitor: one panel per output, each showing its own workspace maps and window list (per-output scoped, matching sway/waybar muscle memory).
- Per-output duplication: the clock appears on every panel since it reads harmlessly.
- Singletons: the system tray and command palette are shown on only one output at a time — whichever has input focus. They follow focus as the user moves between monitors, similar to macOS's Dock teleporting. This avoids duplication and ambiguity about which screen's tray/clock to use.
- Jitter mitigation: only the genuinely-singleton bits (tray, command mode) migrate on focus change. The clock stays duplicated everywhere so every screen remains legible independently.
- Command mode is inherently a singleton (one search invocation) and always appears on the active output regardless of other layout settings.
- Redraw: damage-aware; the clock ticks once a minute, the window list on toplevel events, the workspace maps on layout-change events.

### 3.5 Service dependencies

`CompositorService` (workspaces + per-window geometry for maps), `ForeignToplevel` (window list + command-mode window scope), `Battery`/`Audio`/`Network` (tray), `Clock`, `.desktop` index (command-mode app scope).

---

## 4. Design 2 — unified vertical panel, two modes (DEFERRED)

A single full-height committed panel on the **left** edge that is the entire shell (no top bar). Two display modes; the panel morphs between them.

### 4.1 Compact mode (resting)

Width **56px**, icon-only, Unity-like. Always reserves this width as the exclusive zone. Top-to-bottom: launcher tile, separator, running-app tiles (active app gets a left-edge accent pip), spacer, stacked tray glyphs, compact time (11px).

### 4.2 Expanded mode (engaged)

Width **~190px**. Reveals: a search field (top), a workspace pip row, a separator, a **labelled vertical window list** (icon + title, active highlighted), spacer, a horizontal tray row, and a **large clock** (22px time over 11px full date).

### 4.3 Multi-monitor behaviour

Every output gets its own compact dock (its own running apps and workspace pips), resolving per-output and avoiding global workspace numbering ambiguity.

The expanded panel is not tied to the primary output. It appears on whichever output you invoke it from (hover, keyboard focus, or keybind), then collapses back when dismissed. This keeps the light mode cheaply duplicated while the heavy mode exists as a single instance that follows the user.

This mirrors the Design 1 mental model — per-output structure combined with a singleton, focus-following surface — so the "which output is focused / engaged" plumbing is shared between both layouts.

### 4.4 Reuse

Same services, same tokens, same window-list and search logic as Design 1. It is an alternate layout selected by config, not a separate product.

---

## 5. Notifications and OSD

A single unified surface type for both application notifications (via `org.freedesktop.Notifications`) and system OSD (volume, brightness, session warnings). macOS-style visual language makes the two feel like the same system; same location, same treatment.

### 5.1 Placement

- **Design 1 (horizontal, bottom):** anchored at the bottom-right of the panel, just above the tray. This is the mirror of how most systems place toasts vs the tray — visible but not obstructive.
- **Design 2 (vertical, left):** anchored at the bottom-left of the screen, below/near the compact dock. Same screen-relative corner as Design 1's tray-side position.

### 5.2 Style and timing

- Shared design tokens (§2). OSD and notifications use the same surface, typography, and accent treatment.
- Dismissal: auto-timeout plus click-to-dismiss.
- Hover expansion: the notification expands on hover to reveal full body text / details without opening a separate viewer.
- Interactive actions (reply, dismiss button, etc.) deferred to a later milestone.

### 5.3 Protocol and extensibility

- Baseline compatibility: speak `org.freedesktop.Notifications` on the wire so existing apps work unmodified.
- Custom protocol layered on top for richer content (rich media, Steam-style hints/sender identity, previews) and future interactive actions. The shell is the bridge — the custom protocol is not a replacement for the standard one.
- Steam notification support is a known future target; design should accommodate sender identity and image previews.

---

## 6. Cross-cutting decisions

- **Single accent.** One accent colour (`#7aa2f7`) carries selection/active/launcher everywhere. Status colours are reserved for app glyphs and semantic states.
- **Search is shared.** Command mode (Design 1) and the expanded search (Design 2) are the same launcher subsystem rendered into different surfaces.
- **Layout switch.** `layout = horizontal | vertical` is a top-level config key. Both build on the same component/service layer.
- **Theme.** Nocturne is the default; tokens in §2 are the single source the renderer reads.

---

## 7. Open questions / deferred

- Command-mode actions: source is partially defined (system utilities such as log-out, suspend), but custom-command mechanism remains TBD.
- System tray (StatusNotifierItem) targeting M8. MangoWC compositor support for `xdg-systemtray-v1` must be confirmed or ruled out before that milestone starts; if available it becomes the primary protocol with SNI as fallback, otherwise SNI is primary and `xdg-systemtray-v1` is an experiment.

## 8. Decisions recorded for reference

- **Design 1 anchor:** `bottom` is the default. `top` is not restricted — either direction is a valid configuration choice and neither is structurally privileged.
- **Design 2 expanded overlay vs push:** developing as overlay for now. UX is unproven; this is an unresolved question pending usability testing and may be swapped to push once the feature is ready for evaluation.

---

## 9. Build-order mapping

| Piece | Milestone |
|---|---|
| Layer-surface foundation, panel on each output | M1 (done) |
| Renderer + text + design tokens (§2) → styled panel | M2 |
| Services foundation (battery, clock) → tray + clock | M3 |
| Compositor IPC → workspaces; tiling maps (§3.1) | M4 |
| Window list (foreign-toplevel) → window buttons | M4 |
| Command mode (§3.2) | M5 |
| Design 2 vertical layout + morph (§4) | post-M5 |
