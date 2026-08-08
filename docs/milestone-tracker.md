# bezel — Milestone Tracker

**Version:** 0.1
**Date:** 2026-06-08
**Design reference:** [`BEZEL_DESIGN_SPEC.md`](BEZEL_DESIGN_SPEC.md)
**Build:** `meson setup buildDir && meson compile -C buildDir`

---

## Status overview

| Milestone | Status | Est. complexity |
|-----------|--------|-----------------|
| M1 — Foundation | ✅ DONE | — |
| M2 — Render core | ✅ DONE | High |
| M3 — Services + first module | ✅ DONE | Medium |
| M4 — Compositor backend | ✅ DONE | High |
| M5 — Launcher | ✅ DONE | Medium |
| M6 — Notifications | ✅ DONE | Medium |
| M7 — OSD + status polish | ✅ DONE | Medium |
| M8 — System tray | ✅ DONE | High |
| M9 — Session: idle + lock + power | 📋 PLANNED | High |
| M10 — Cohesion pass | 📋 PLANNED | Medium |

> **Note:** The status table above is current, but the detailed milestone
> sections below still reflect the original planning document. M2–M7 have been
> implemented; their subsections and TODO lists are stale and should be treated
> as historical reference only.

---

## Dependency graph

```
M1 (Foundation) ──── DONE
  │
  ├──▶ M2 (Render core)
  │      │
  │      ├──▶ M3 (Services + first module)
  │      │      │
  │      │      ├──▶ M4 (Compositor backend)
  │      │      │      │
  │      │      │      └──▶ M5 (Launcher)
  │      │      │
  │      │      ├──▶ M6 (Notifications)
  │      │      │
  │      │      ├──▶ M7 (OSD + status polish)
  │      │      │
  │      │      └──▶ M8 (System tray)
  │      │
  │      └──▶ M4 (Compositor backend) ── also depends on M3 (observer pattern)
  │
  └──▶ M9 (Session: idle + lock + power) ── needs M3 (event loop integration), needs Wayland protocols from M4
            │
            └──▶ M10 (Cohesion pass) ── waits for everything above
```

**Parallelism:** M6, M7, and M8 are independent of each other once M3 is done. M5 depends on M4. M9 depends on M3 and M4 (and on M2 for rendering). M10 is the finishing pass.

---

## M1 — Foundation ✅ DONE

**Goal:** Single-process Wayland panel. One layer surface per output that clears to a colour.

**Architecture delivered:**
- `wl_registry` listener → binds `wl_compositor`, `zwlr_layer_shell_v1`, `wl_output`
- `App` class owns the event loop (`wl_display_dispatch`), registry, output list
- `Egl` class wraps `eglGetPlatformDisplayEXT` + config/context init (GLES2)
- `Panel` class creates a top-left-right-anchored layer surface with exclusive zone, creates an `wl_egl_window`, clears to `#1f1f24` each render

**Files:**
| File | Role |
|------|------|
| `src/main.cpp` | Entry point, `App`, registry, output lifecycle |
| `src/panel.cpp` / `.hpp` | Layer surface + EGL window per output |
| `src/egl.cpp` / `.hpp` | EGL display/context |
| `src/protocol.hpp` | Include-ordering wrapper for generated protocol header |
| `protocols/wlr-layer-shell-unstable-v1.xml` | Protocol XML |
| `meson.build` | Build definition |

**Design spec reference:** §9 — "Layer-surface foundation, panel on each output → M1 (done)"

**Post-M1 drift from spec:**
- Panel height is currently `32px`; design spec §3.1 calls for `48px`. This should be bumped in M2 when the clock renders.
- `glClearColor` uses `(0.12, 0.12, 0.14, 1.0)` which is close to `panel.bg` (`#16161e` ≈ `0.086, 0.086, 0.118`). Update to match design tokens in M2.

---

## M2 — Render core 🔜 NEXT

**Goal:** Replace the raw GL clear with a NanoVG + FreeType/HarfBuzz text stack. Introduce a widget tree, layout engine, and theme tokens. The panel shows a styled clock on every monitor.

**Dependencies:** M1 (Foundation)

**Design spec references:**
- §2 — Global design tokens (colours, typography, geometry)
- §3.1 — Default mode layout (clock at right end)
- §3.4 — Clock ticks once per minute; damage-aware redraws
- §3.4 — Clock appears on every panel (harmless duplication)

### TODOs

#### 2.1 Dependencies

- [ ] Add `nanovg` (stock memononen/nanovg, GLES2 backend `nanovg_gl.h`) as a Meson wrap or vendored dependency
- [ ] Add `freetype2` dependency to `meson.build`
- [ ] Add `harfbuzz` dependency to `meson.build`
- [ ] Verify all three compile and link against GLES2

#### 2.2 Render pipeline

- [ ] Create `Renderer` class (one per `Panel`) that:
  - Initialises a NanoVG `NVGcontext` from the EGL context (GLES2 path)
  - Begins/ends frames around panel `render()` calls
  - Handles HiDPI scaling via `nvgGlobalScale()`
- [ ] Factor out `Panel::render()` raw GL calls; panel delegates to `Renderer`
- [ ] Expose a `FontCache` that loads `.ttf` via FreeType into NanoVG font handles; HarfBuzz used for shaping when needed (RTL, complex scripts)
- [ ] Ensure fonts fall back: system sans `/usr/share/fonts` → built-in Inter-class if unavailable

#### 2.3 Widget base class + layout

- [ ] Define `Widget` base class with:
  - `width()`, `height()`, `preferredSize()` virtuals
  - `layout(x, y, available_width, available_height)` — position self and recurse into children
  - `render(renderer)` — draw self and children
  - `hit_test(mx, my)` — for future pointer input
- [ ] Implement `BoxLayout` widget (horizontal and vertical orientation):
  - Distributes children according to their preferred sizes
  - Supports `Flex` child weight for proportional allocation
  - Supports `Spacer` that greedily fills remaining space
  - Supports fixed `gap` between children and `padding` around edges
- [ ] Implement `Label` widget:
  - `setText(const std::string&)`
  - `setFontSize(float)`, `setColor(NVGcolor)`, `setAlign(LEFT|CENTER|RIGHT)`
  - Returns text extents for preferred size calculation

#### 2.4 Theme tokens

- [ ] Create `Theme` struct with all tokens from design spec §2:
  - Colours: `panelBg`, `panelBgElevated`, `textPrimary`, `textSecondary`, `textMuted`, `accent`, `accentTint`, `borderHairline`, `borderEmphasis`, `borderAccent`
  - Status accents: `green`, `cyan`, `magenta`, `yellow`, `red`
  - Fonts: `uiSans`, `mono` — font IDs after loading
  - Sizes: `labelPrimary` (12px), `labelSecondary` (11px), `largeClock` (22px), `minFontSize` (11px)
  - Weights: `regular` (400), `active` (500)
  - Geometry: key radius/size/pad/gap values
- [ ] Instantiate a single `Theme` in `App`, pass reference to every `Panel → Renderer`
- [ ] Apply tokens: `glClearColor` to `panel.bg`, panel height to `48px`

#### 2.5 Clock widget

- [ ] Create `Clock` widget (subclass of `Widget`):
  - Fetches `std::chrono::system_clock::now()` once per render
  - Formats time (`12px`) and date (`11px muted`) stacked vertically
  - Tick gating: skips re-formatting if the minute hasn't changed since last render
  - Returns preferred size from the stacked label extents
- [ ] Wire clock into default panel layout:
  ```
  [x---------gap---------x] [Spacer] [Clock]
  ```
  `Spacer` absorbs remaining width; clock sits at right end.

#### 2.6 Per-panel layout

- [ ] Each `Panel` owns a root `BoxLayout` widget
- [ ] `Panel::render ()` calls `root->layout(0, 0, width, height)` → `root->render(renderer)`
- [ ] Each output gets an identical widget tree (clock duplicated per spec §3.4)

### Deliverables

- Panel clears to `#16161e` (correct `panel.bg` token), height `48px`
- A right-aligned clock on every monitor showing time over date
- Clock redraws only on minute change (damage-aware)
- Theme tokens accessible as structured data, read from a single source
- Widget/layout system ready to accept new components (M3 battery, M4 workspace pills, etc.)

---

## M3 — Services + first module 📋 PLANNED

**Goal:** Integrate sd-bus (via sdbus-c++) into the Wayland event loop. Implement `BatteryService` that polls UPower. Wire its updates to a battery widget on the panel. This proves the **service→component observer pattern** that every subsequent milestone reuses.

**Dependencies:** M2 (Render core — needs widgets + layout to display battery)

**Design spec references:**
- §3.1 — System tray: status glyphs (battery alongside network, volume)
- §3.4 — Redraw on battery events (not just clock ticks)
- §3.5 — `Battery` listed as a service dependency

### TODOs

#### 3.1 sd-bus event loop integration

- [ ] Add `sdbus-c++` dependency to `meson.build`
- [ ] Create `DbusEventLoop`:
  - Wraps `sdbus::createSystemBus()`
  - Exposes the sd-bus fd (`sd_bus_get_fd()`) as a Wayland event source
  - Registers in the Wayland event loop alongside `wl_display`
    - Option A: Add the fd to `wl_event_loop` (use `wl_display_get_event_loop()` and `wl_event_loop_add_fd()`)
    - Option B: Poll manually in `App::run()` — avoid this, keep dispatch clean
  - **Design decision needed:** MangoWC may or may not expose `wl_event_loop`. Default to a manual `poll()` loop in `App::run()` that watches both the Wayland fd and D-Bus fd. This is less elegant but guarantees portability across compositors.
- [ ] Verify `DbusEventLoop` processes incoming D-Bus messages during each `App::run()` iteration

#### 3.2 Service base class + observer pattern

- [ ] Define abstract `Service` class:
  - `init()` — called once after construction
  - `tick()` — called each frame (for polling services); returns `bool` (true = state changed)
  - Each `Service` owns a list of `std::function<void()>` observer callbacks
  - `subscribe(std::function<void()>)` / `unsubscribe(id)` methods
  - `notify()` called when state changes, triggers all observers
- [ ] Define `BatteryInfo` struct: `percentage` (0-100), `state` (charging/discharging/full/unknown), `timeToEmpty`, `timeToFull`
- [ ] Define `BatteryService : public Service`:
  - Connects to `org.freedesktop.UPower` on the system bus
  - Calls `EnumerateDevices()` to find the display device (`DisplayDevice` property)
  - Subscribes to `PropertiesChanged` signal on the display device
  - Parses `Percentage`, `State`, `TimeToEmpty`, `TimeToFull` properties on change
  - `tick()` returns `true` when new data arrives (push from signal, no polling)
- [ ] Wire `BatteryService` lifecycle into `App`:
  - Construct after M3 init, before panels
  - Call `service->tick()` each frame in `App::run()`
  - Each interested `Panel` subscribes via `service->subscribe(callback)`

#### 3.3 Battery widget

- [ ] Create `BatteryWidget`:
  - Displays percentage (e.g., `85%`, `12px primary`)
  - Displays a glyph or text indicator for charging state (bolt `⚡` / plug symbol)
  - Returns preferred size for layout
  - Subscribes to `BatteryService` via observer callback; calls `Panel::requestRedraw()` on change
- [ ] Insert `BatteryWidget` into panel layout, left of the clock:
  ```
  [...future widgets...] [Battery] [gap] [Clock]
  ```

#### 3.4 Redraw trigger mechanism

- [ ] Add `Panel::requestRedraw()` — sets a dirty flag
- [ ] In `App::run()`, after dispatching Wayland + D-Bus, iterate outputs and call `render()` on dirty panels
- [ ] Clock uses the same mechanism (minute-change sets dirty)

### Deliverables

- D-Bus messages are dispatched interleaved with Wayland events in a single `poll()` loop
- Battery percentage and charge state render on the panel, updating live as UPower emits
- `Service` base class and observer pattern are established; adding `NetworkService`, `AudioService`, `MprisService` later is mechanical
- Panel redraws are demand-driven (clock tick + service events), not on a blind timer

---

## M4 — Compositor backend 📋 PLANNED

**Goal:** Speak compositor IPC to get workspace metadata and per-window titles. The bar becomes WM-aware: workspace pills with tiling maps, a window list, and the active window title.

**Dependencies:** M3 (Services + observer pattern — `CompositorService` extends the same `Service` base)

**Design spec references:**
- §3.1 — Workspace switcher with tiling maps (36×26 boxes, accent border on active, sub-rectangles for windows)
- §3.1 — Window list (icon + title, active underline)
- §3.1 — Spacer between window list and tray
- §3.3 — Toggle: `tiling-maps` (default) vs `flat-icons`
- §3.4 — Per-output scoping: each panel shows its own workspace maps + window list
- §3.5 — `CompositorService`, `ForeignToplevel` listed as dependencies

### TODOs

#### 4.1 Protocol support

- [ ] Add `wlr-foreign-toplevel-management-unstable-v1.xml` to `protocols/`
- [ ] Update `meson.build` protocols list
- [ ] Add generated header include (with namespace hack if needed) to `protocol.hpp`
- [ ] Add `ext-workspace-unstable-v1.xml` to `protocols/` (wlroots ext-workspace protocol)
- [ ] Update `meson.build` accordingly

#### 4.2 Foreign toplevel service

- [x] Create `ToplevelService : public Service`:
  - Binds `zwlr_foreign_toplevel_manager_v1` from registry
  - Listens for `toplevel` creation events → tracks `Toplevel` objects (title, app_id, state)
  - Listens for `finished` events → removes from the list
  - Emits observer notifications on any toplevel change (new/closed/title-changed/focus-changed)
  - Exposes `std::vector<ToplevelInfo>` ordered by creation or by spatial layout if available
  - `ToplevelInfo` struct: `title`, `app_id`, `activated` (bool), `parent` (optional wl_output reference for per-output filtering)

#### 4.3 Workspace service + MangoWC IPC

- [x] Create `WorkspaceService : public Service`:
  - Bind `ext_workspace_manager_v1` from registry if available
  - Fall back: connect to MangoWC custom IPC (Unix socket or D-Bus, TBD once MangoWC IPC is defined)
  - For MangoWC IPC path: query workspace list, window geometry per workspace, active workspace
  - Exposes `std::vector<WorkspaceInfo>` with per-workspace window rects
  - `WorkspaceInfo` struct: `name`/`index`, `active` (bool), `tiles: vector<Rect>` (proportional coordinates for tiling map rendering)
- [x] **Risk:** MangoWC IPC details unknown. If no IPC is available, fall back to `flat-icons` mode (workspace indices only, extracted from ext-workspace protocol or compositor globals). **Do not block M4 on MangoWC IPC — ship with flat-icons first, upgrade to tiling maps when IPC is available.**

#### 4.4 Workspace switcher widget

- [x] Create `WorkspaceSwitcher` widget:
  - Subscribes to `WorkspaceService`
  - **Tiling-map mode** (default when layout data available): renders 36×26 boxes per workspace. Each box draws proportional sub-rectangles (darker fill) mirroring window layout. Active workspace gets accent border + brighter fill. Click-to-focus (pointer input deferred to M5/M9).
  - **Flat-icons mode** (fallback): renders numbered/lettered workspace pips. Active pip filled with accent.
  - Hit regions stored for future click handling (`mappedWorkspaces[i].bounds`)
- [x] Layout: inserted after the launcher trigger tile (M5), before the window list

#### 4.5 Window list widget

- [ ] Create `WindowList` widget:
  - Subscribes to `ToplevelService`
  - Renders labelled buttons (icon + title) for each toplevel, Windows-style
  - Active window: elevated fill (`panel.bg.elevated`) + 2px accent underline
  - Inactive windows: `text.secondary`
  - Overflow: if total width exceeds available space, truncate labels with ellipsis (no scrolling yet)
  - **Per-output filtering:** filter toplevels by the output the toplevel is on (if protocol supports it) or show all
- [ ] Layout: between workspace switcher and Spacer

#### 4.6 Per-output scoping

- [ ] Each `Panel` receives a reference to its bound `wl_output*`
- [ ] `ToplevelService` and `WorkspaceService` are singletons in `App`; each `Panel` subscribes and filters according to its output
- [ ] The spacer pushes tray + clock to the right; window list + workspace switcher stay left

### Deliverables

- Workspace pills render on each panel, showing status from the compositor
- Tiling map mode shows proportional window layout per workspace (if IPC supports it)
- Window list shows running apps with active-window highlight
- Panel layout is:
  ```
  [WorkspaceSwitcher] [WindowList] [Spacer] [Battery (M3)] [Clock (M2)]
  ```
- Per-output filtering ensures each panel shows only its output's windows (if protocol supports it)

---

## M5 — Launcher 📋 PLANNED

**Goal:** On-demand keyboard-interactive overlay. Parses `.desktop` files. Fuzzy search across apps, open windows, and commands. Launches via `posix_spawn`. Surface is spawned and destroyed dynamically, driven by a Unix domain socket.

**Dependencies:** M4 (Compositor backend — needs window list for the "open windows" search scope)

**Design spec references:**
- §3.1 — Command trigger tile (search glyph, accent colored)
- §3.2 — Command mode: surface grows to 62px, query field, ranked results
- §3.4 — Command mode is a singleton, always on the active output
- §3.5 — `.desktop` index for app scope

### TODOs

#### 5.1 .desktop parser

- [ ] Create `DesktopIndex` class:
  - Scans `~/.local/share/applications/` and `/usr/share/applications/` on startup
  - Parses `.desktop` files: `Name`, `Comment`, `Exec`, `Icon`, `NoDisplay`, `Hidden`, `OnlyShowIn`/`NotShowIn`
  - Skips entries with `NoDisplay=true`, `Hidden=true`, or mismatched desktop environment filter
  - Stores `DesktopEntry` struct per result
  - Re-scans on `SIGHUP` or config reload (M10)
- [ ] Parse `Exec` field: strip `%f`, `%F`, `%u`, `%U`, `%i`, `%c`, `%k` field codes; leave the executable + static arguments

#### 5.2 Control socket

- [ ] Create `ControlSocket` class in `App`:
  - Binds a Unix domain socket at `$XDG_RUNTIME_DIR/bezel.sock` (or `/tmp/bezel.sock` fallback)
  - Listens for clients (a hotkey daemon like `sxhkd` or `swaymsg`)
  - Supports commands:
    - `toggle_launcher` — open/close the command mode surface on the active output
    - Future: `reload_config`, `lock`, `power_action suspend|poweroff|reboot`
  - Dispatch to `App` callback; socket is watched in the main `poll()` loop alongside Wayland + D-Bus

#### 5.3 Fuzzy search

- [ ] Implement fuzzy matching:
  - Algorithm: simple character-skip match (subsequence). Each character of the query must appear in order in the candidate string.
  - Score: contiguous runs boost score, word-boundary starts boost score. Lowercase match against lowercase.
  - Special case: query starting with `>` or `.` restricts scope to actions/commands.
- [ ] Create `SearchEngine`:
  - Three scopes merged into one ranked list:
    1. **Applications** (from `DesktopIndex`)
    2. **Open windows** (from `ToplevelService`; exclude the currently-focused window per §3.2)
    3. **Commands/actions** (log-out, suspend, lock, quit bezel — hardcoded initially, extensible later)
  - Ranking: apps first, windows second, commands third (§3.2)
  - Each result: `{icon, name, type_subtitle, activation_target}`

#### 5.4 Command mode surface

- [ ] Create `LauncherPanel` (extends or uses same surface machinery as `Panel`):
  - **Singleton:** only one instance in `App`, created on first `toggle_launcher`, destroyed on dismiss
  - Appears on the **active output** (determined by focused toplevel's output or the output under the pointer)
  - Anchored same as the base panel (bottom, left, right), but with height `62px` (§3.2)
  - Higher exclusive zone than the base panel so it stacks above it
- [ ] Render content:
  - Query field on the left: calls `zwlr_layer_surface_v1_set_keyboard_interactivity(1)` to grab keyboard
  - Results list: horizontal row of `{icon, name(12px), type_subtitle(11px muted)}`. First result pre-selected with accent fill.
  - Enter activates selection; `Esc` dismisses and returns surface to base panel mode
  - Keyboard input: backspace deletes, printable chars append, arrow keys navigate selection
- [ ] Activation:
  - App activation: `fork()` + `execvp()` the parsed `.desktop` `Exec` string
  - Window activation: send activate request via `wlr-foreign-toplevel`
  - Command activation: call internal action handler

#### 5.5 Input handling

- [ ] Create `InputBackend` class:
  - Binds `wl_seat` from registry
  - Listens for `wl_keyboard` enter/leave and key events
  - Routes key events only to the focused panel (command mode surface when active)
  - Maps keycodes to keysyms via `xkbcommon`
- [ ] Handle the `Super` key gracefully: when command mode is dismissed, release keyboard grab so the compositor can process `Super` again

### Deliverables

- `echo "toggle_launcher" | nc -U $XDG_RUNTIME_DIR/bezel.sock` opens the command mode surface
- The surface appears on the active output, requests keyboard focus, and renders a search field
- Typing fuzzy-matches across .desktop apps, open windows, and system commands
- First result is pre-selected; Enter launches it; Esc dismisses
- The surface is destroyed on dismiss (no idle overlay eating resources)
- `.desktop` index loads on startup and is available for querying

---

## M6 — Notifications 📋 PLANNED

**Goal:** Implement the `org.freedesktop.Notifications` D-Bus interface as a service provider (not consumer). Render stacked toasts with timeouts and basic actions. This is the first time bezel acts as a server on D-Bus.

**Dependencies:** M3 (Services + observer pattern, D-Bus event loop)

**Design spec references:**
- §5 — Notifications and OSD (shared visual language, placement, style, timing)
- §5.1 — Placement: bottom-right of panel, just above the tray (Design 1)
- §5.2 — Dismissal: auto-timeout + click-to-dismiss; hover expands to show full body
- §5.3 — Baseline `org.freedesktop.Notifications` compatibility

### TODOs

#### 6.1 D-Bus interface registration

- [ ] Create `NotificationService` (extends `Service`, but runs as a D-Bus *server*):
  - Register the name `org.freedesktop.Notifications` on the session bus
  - Implement the `org.freedesktop.Notifications` interface:
    - `GetCapabilities() → as` — return `["body", "actions", "icon-static"]` (no markup, no persistence yet)
    - `Notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout) → u` — returns notification ID
    - `CloseNotification(id)`
    - `GetServerInformation() → (name, vendor, version, spec_version)` — return `("bezel", "bezel", "0.1", "1.2")`
  - Emit `NotificationClosed(id, reason)` and `ActionInvoked(id, action_key)` signals appropriately

#### 6.2 Toast rendering

- [ ] Create `ToastWidget`:
  - Renders a notification card: `app_icon` (optional, left), `summary` (12px, `text.primary`), `body` (11px, `text.secondary`)
  - Background: `panel.bg` with `border.hairline` edge
  - Corner radius: `radius.tile` (8px)
  - Max width: ~360px; body truncated to 2 lines
- [ ] Create `NotificationOverlay` (holds stacked toasts):
  - Anchored at bottom-right of the screen, positioned above the panel's exclusive zone
  - **Not** a dedicated layer surface — rendered into a child layer surface with anchor bottom-right
  - Stacks toasts vertically with `gap.item` spacing
  - New toasts slide in from the right (deferred animation to M10, instant appear for M6)
  - On hover: expand to show full body text (non-truncated)
  - On click: dismiss (call `NotificationClosed(expired)`)

#### 6.3 Timeout management

- [ ] Each toast stores its `expire_timeout` (from the notification; default 5s if not specified)
- [ ] Track elapsed time per toast in the main loop tick
- [ ] On expiry: animate out (deferred to M10; instant removal for M6), emit `NotificationClosed(expired)`
- [ ] Max concurrent toasts: cap at 3 visible. Queue extras as FIFO; a dismissed/expired slot pops the next queued toast.

#### 6.4 Actions (basic)

- [ ] Parse the `actions` array from `Notify()` (alternating action_key / action_label strings)
- [ ] Render action buttons on the toast (e.g., "Reply", "Dismiss"):
  - Max 2 buttons inline
  - `text.accent` styling on hover
- [ ] On action button click: emit `ActionInvoked(id, action_key)` signal, then dismiss the toast

### Deliverables

- `notify-send "Hello" "World"` produces a visible toast at bottom-right
- Multiple toasts stack vertically
- Toasts auto-dismiss after their timeout
- Click dismisses a toast
- Hover expands truncated body text
- Basic action buttons work (reply/dismiss)

---

## M7 — OSD + status polish 📋 PLANNED

**Goal:** Volume and brightness popups via PipeWire, a network module via NetworkManager, and an MPRIS media module. All reuse the observer pattern and D-Bus plumbing from M3.

**Dependencies:** M3 (Services + observer), M2 (widget system)

**Design spec references:**
- §3.1 — System tray: status glyphs (network, volume)
- §5 — OSD shares visual language with notifications (same surface type, same treatment)
- §5.1 — Placement mirrors notification placement

### TODOs

#### 7.1 PipeWire audio service

- [ ] Add `libpipewire-0.3` dependency to `meson.build`
- [ ] Create `AudioService : public Service`:
  - Connects to PipeWire as a client
  - Subscribes to default sink volume changes
  - Exposes `volume` (0.0–1.0) and `muted` (bool)
  - Emits observer notifications on change
- [ ] Create `VolumeOsd` (OSD popup, not a permanent panel widget):
  - Same surface type and visual language as `NotificationOverlay` from M6
  - Renders a volume bar (accent fill proportional to volume) + mute icon
  - Auto-dismiss after 2s
  - Triggered by volume change events from `AudioService`
- [ ] Wire `AudioService` as a permanent panel glyph: a small volume icon in the tray area (speaker with level indicator)

#### 7.2 Brightness (backlight)

- [ ] Create `BrightnessService : public Service`:
  - Probes `/sys/class/backlight/` for devices
  - Reads `actual_brightness` / `max_brightness`
  - Watches for changes via `inotify` on the sysfs file
  - **Alternative:** connect to `org.freedesktop.login1` or `org.freedesktop.UPower.KbdBacklight` via D-Bus if sysfs is insufficient
  - Emits observer notifications on brightness change
- [ ] Create `BrightnessOsd` (same OSD pattern as volume bar)

#### 7.3 Network manager

- [ ] Create `NetworkService : public Service`:
  - Connects to `org.freedesktop.NetworkManager` on the system bus via sdbus-c++
  - Queries active connections: SSID for Wi-Fi, interface name for wired
  - Subscribes to `PropertiesChanged` on `org.freedesktop.NetworkManager`
  - Exposes `NetworkInfo`: `type` (wifi/wired/none), `ssid`, `strength` (0–100), `icon` (glyph)
- [ ] Create `NetworkWidget` (permanent panel glyph in the tray area):
  - Wi-Fi: signal-strength icon + optional SSID text
  - Wired: ethernet icon
  - None: disconnected icon
- [ ] Layout the tray area:
  ```
  [NetworkWidget] [VolumeWidget] [BatteryWidget] [Clock]
  ```

#### 7.4 MPRIS media module

- [ ] Create `MprisService : public Service`:
  - Watches for `org.mpris.MediaPlayer2.*` names on the session bus
  - Connects to the most-recently-active player (or all, user-configurable)
  - Subscribes to `PropertiesChanged` on `org.mpris.MediaPlayer2.Player`
  - Exposes `MprisInfo`: `title`, `artist`, `album`, `artUrl`, `playbackStatus` (Playing/Paused/Stopped)
  - Emits observer notifications on change
- [ ] Create `MprisWidget` (permanent panel widget):
  - Shows artwork (thumbnail, 20px) + title/artist (12px primary over 11px muted), truncated to fit
  - Play/pause glyph based on playback status
- [ ] Layout placement: between window list and spacer (left of the tray), or as part of the tray area

### Deliverables

- Volume OSD pops up on volume change; permanent volume glyph in tray reflects mute state
- Brightness OSD pops up on brightness change
- Wi-Fi SSID and signal strength visible in tray; updates live
- MPRIS track info appears on panel when media is playing
- All five services (battery, audio, brightness, network, MPRIS) share the same observer pattern

---

## M8 — System tray 📋 PLANNED

**Goal:** Implement StatusNotifierWatcher (host) and StatusNotifierItem rendering. Application tray icons appear and are interactive.

**Dependencies:** M3 (Services + observer, D-Bus), M2 (widget system, pointer input from M5/M9)

**Design spec references:**
- §3.1 — System tray (tray area at right)
- §3.4 — Tray is a singleton, teleports to the active output
- §3.4 — Tray migrates on focus change

**Reference:** MangoWC users configure waybar with `libappindicator` and `xdg-desktop-portal-wlr` for tray support, indicating KDE StatusNotifierItem (SNI) on D-Bus is the historically proven path. SNI is the primary approach unless MangoWC is confirmed to support `xdg-systemtray-v1`, in which case that becomes primary. Either way, the shell is the bridge — no protocol is replaced by the other; both can coexist in the service layer.

### TODOs

#### 8.1 D-Bus watcher host

- [ ] Create `TrayService : public Service`:
  - Register `org.kde.StatusNotifierWatcher` on the session bus
  - Implement `RegisterStatusNotifierItem(service)` — a client app registers its SNI service name
  - Track registered items in a list
  - Implement `RegisterStatusNotifierHost(service)` (no-op, we are the host)
  - Expose `IsStatusNotifierHostRegistered` property (return true)
  - Emit observer notifications when items register/unregister
  - Implement `ProtocolVersion` property (return 1)

#### 8.2 StatusNotifierItem proxy

- [ ] Create `SniItem` class (per registered item):
  - Connects to the item's D-Bus service via sdbus-c++ proxy
  - Reads properties: `Id`, `Title`, `IconName`, `IconThemePath`, `IconPixmap`, `Status` (Active/Passive/NeedsAttention), `Category`, `Menu` (object path), `ItemIsMenu`, `ToolTip`
  - Subscribes to `NewIcon`, `NewAttentionIcon`, `NewStatus`, `NewTitle`, `NewToolTip` signals
  - Parses `IconPixmap` (array of `(width, height, bytes)` — ARGB32 raw pixel data)
  - Falls back: if no `IconPixmap`, call `org.freedesktop.DBus.Properties.Get` on `IconThemePath` + `IconName` → load PNG/XPM from the theme path or from `hicolor`

#### 8.3 Icon rendering

- [ ] Create `SniIconWidget`:
  - Converts `IconPixmap` ARGB32 bytes to a NanoVG image handle (`nvgCreateImageRGBA()`)
  - Caches the NanoVG image handle; re-creates on `NewIcon` signal
  - Renders at 20×20px (tweakable via design tokens `icon.inline`)
  - Renders tooltip on hover (text label with `text.muted`, 11px, positioned above the icon)
  - Click-to-activate: call `ContextMenu(x, y)` on the item (if `ItemIsMenu` is false) or trigger the D-Bus menu (deferred)
- [ ] Layout: `SniIconWidget` instances are placed in the tray area segment of the panel, left of the battery/network/volume glyphs

#### 8.4 Singleton tray with focus teleport

Per design spec §3.4:
- [ ] Only **one** output's panel renders the system tray at any time
- [ ] The tray appears on whichever output currently has input focus (or pointer focus)
- [ ] On focus change:
  - The previously-active panel hides its tray widgets (layout recalculates)
  - The newly-active panel renders the tray widgets in its layout
  - Clock, battery, network, volume remain duplicated on all panels per §3.4
- [ ] Implementation: `TrayService` calls `Panel::requestFullRelayout()` on both the old and new active panels when focus changes

#### 8.5 Known complexity

- [ ] **Icon format handling:** SNI specifies `IconPixmap` as ARGB32. This must be byte-swapped if the host endianness differs. Verify endianness handling.
- [ ] **Icon caching:** avoid re-creating NanoVG image handles on every render. Cache per-`SniItem`.
- [ ] **DBusMenu:** Items with `ItemIsMenu=true` expose `com.canonical.dbusmenu` on their menu path. Rendering native DBusMenu trees in a Wayland surface is complex. Defer interactive context menus to post-M10; support `ItemIsMenu=false` (click sends `ContextMenu`) only for M8.
- [ ] **Multiple items:** cap tray icon count to ~8 visible items. Overflow items get a "show more" chevron (expandable popover — deferred).

### Deliverables

- Tray icons from apps that speak KDE SNI (e.g., `nm-applet`, `blueman-applet`) render in the tray area
- Icons update when apps replace their pixmap
- Tooltips render on hover
- Tray teleports between outputs when focus changes
- Status glyphs (battery, network, volume) render to the left of SNI icons

---

## M9 — Session: idle + lock + power 📋 PLANNED

**Goal:** Idle detection, a secure session lock with PAM, and a power menu backed by logind. This makes bezel a full session shell — not just a panel.

**Dependencies:** M3 (D-Bus event loop), M2 (widget/render), Wayland protocol infrastructure from M4

**Design spec references:**
- §7 — Open questions mention session utilities (log-out, suspend)
- Lock design is not in the design spec yet; this milestone defines it

### TODOs

#### 9.1 ext-idle-notify-v1

- [ ] Add `ext-idle-notify-v1.xml` to `protocols/`
- [ ] Update `meson.build` protocols list
- [ ] Create `IdleService : public Service`:
  - Binds `ext_idle_notifier_v1` from registry
  - Creates an `ext_idle_notification_v1` with a configurable timeout (default 300s)
  - Listens for `idled` and `resumed` events
  - Exposes `idle` (bool) state
  - Emits observer notifications on idle/resume

#### 9.2 ext-session-lock-v1 + PAM

**Security-critical:** A broken lock either exposes the desktop or locks the user out. Test thoroughly with a second TTY session.

- [ ] Add `ext-session-lock-v1.xml` to `protocols/`
- [ ] Update `meson.build` protocols list
- [ ] Create `LockService`:
  - Binds `ext_session_lock_manager_v1` from registry
  - On lock request (from control socket or idle timeout): calls `ext_session_lock_manager_v1_lock()` → compositor hides all surfaces and grants the lock surface exclusive render
  - Creates a lock surface: full-screen, topmost layer, opaque. Renders a centered password prompt using the widget system from M2.
  - Keyboard interactivity: grabs keyboard exclusively
- [ ] PAM integration (`libpam`):
  - `#include <security/pam_appl.h>`
  - In-process PAM: initialize a `pam_handle_t` with service name `"bezel"` (requires a PAM config at `/etc/pam.d/bezel` — document this setup)
  - Conversation function: bezel provides the password string via a static callback (`pam_conv`) — the lock surface's input field feeds characters to the PAM conversation
  - On `pam_authenticate()` success → call `ext_session_lock_surface_v1_unlock_and_destroy()` → compositor restores normal rendering
  - On failure → clear password field, show error message, rate-limit attempts (1s delay between tries)
- [ ] Error handling:
  - PAM errors printed to stderr (not visible on lock screen, but useful for debugging)
  - If PAM module fails to load, log the error and refuse to lock (don't soft-lock)
  - Keyboard layout: pass through system keymap via `xkbcommon` — don't assume US layout

#### 9.3 Power menu + logind

- [ ] Create `LogindService : public Service`:
  - Connects to `org.freedesktop.login1` on the system bus via sdbus-c++
  - Exposes methods: `suspend()`, `hibernate()`, `powerOff()`, `reboot()`
  - Each method calls the corresponding logind method with `polkit` interaction flag
  - Exposes `canSuspend`, `canHibernate`, `canPowerOff`, `canReboot` properties
- [ ] Create `PowerMenu` (OSD overlay, same pattern as launcher + notifications):
  - Triggered via control socket (`power_menu` command) or a D-Bus action
  - Renders a centered or right-aligned popup with options: Lock, Suspend, Power Off, Reboot, Cancel
  - Uses accent highlight on hover/selection
  - Keyboard-navigable (arrow keys + Enter)
  - Cancel on `Esc` or click-outside
- [ ] Route lock action from power menu → `LockService::lock()`

#### 9.4 Configurable idle lock

- [ ] In `App::run()` loop: when `IdleService` reports `idle=true` and config `lock.onIdle=true`, call `LockService::lock()`
- [ ] On `resumed` event from idle service, do nothing (the lock surface is already up; user types password to dismiss it)

### Deliverables

- After 5 minutes of inactivity, the screen locks (PAM password prompt)
- Correct password unlocks and restores the session; incorrect password shows error
- `echo "lock" | nc -U $XDG_RUNTIME_DIR/bezel.sock` triggers lock
- Power menu pops up with Suspend/Power Off/Reboot/Lock options
- All power actions work via logind
- A second TTY can `killall bezel` if the lock process fails (safety valve)

---

## M10 — Cohesion pass 📋 PLANNED

**Goal:** Unified config file, complete theming, subtle animations, per-output correctness, damage-tracking performance. This is the finishing pass that makes the shell feel like a product, not a collection of features.

**Dependencies:** Everything above (M1–M9)

**Design spec references:**
- §2 — Design tokens as structured data (becomes the config schema)
- §3.3 — Toggleable options (workspace render mode, anchor, window-button labels)
- §6 — Cross-cutting: single accent, layout switch, theme as single source
- §3.4 — Damage-aware redraws

### TODOs

#### 10.1 Config system (TOML)

- [ ] Add `tomlplusplus` dependency (header-only) to `meson.build`
- [ ] Create `Config` class:
  - Loads from `$XDG_CONFIG_HOME/bezel/config.toml` (fallback `~/.config/bezel/config.toml`)
  - Falls back to compiled-in defaults (Nocturne theme tokens from §2)
  - Exposes structured config: `theme`, `layout`, `modules`, `session`
  - Schema (draft):
    ```toml
    [layout]
    panel_height = 48            # px
    anchor = "bottom"            # "bottom" | "top"
    workspace_mode = "tiling"    # "tiling" | "flat"
    window_labels = "on"         # "on" | "icons-only"
    # "vertical" deferred to post-M10 per design spec §4

    [theme]
    accent = "#7aa2f7"
    panel_bg = "#16161e"
    # ... all tokens from §2 overridable

    [modules]
    clock = true
    battery = true
    network = true
    volume = true
    mpris = true
    tray = true

    [session]
    lock_on_idle = true
    idle_timeout_sec = 300
    ```
- [ ] Wire `Config` into `App`; each milestone's module reads its enabled/disabled state from config
- [ ] SIGHUP triggers config reload: re-read TOML, rebuild widget trees, re-layout panels
- [ ] Watch config file with `inotify` for auto-reload

#### 10.2 Theming completion

- [ ] `Theme` struct is initialized from `[theme]` section of config, falling back to §2 defaults
- [ ] `Renderer` reads all colour/font/geometry tokens from `Theme` at init
- [ ] Verify every widget uses theme tokens, never hardcoded colours
- [ ] Support `[theme]` overrides: user can tweak specific tokens without replacing the whole theme
- [ ] Status accents (green/cyan/magenta/yellow/red) are configurable under `[theme.status]`

#### 10.3 Subtle animations

- [ ] Add animation framework in `Renderer`:
  - Tweened property: `from → to` over `duration` (ms) with easing function
  - Easing: `easeOutCubic`, `easeInOutCubic`
  - Frame-time-based: `current = from + (to - from) * easeInOutCubic(clamp(elapsed / duration, 0, 1))`
- [ ] Apply animations:
  - Toast slide-in: notifications (M6) translate from right over 200ms (easeOutCubic)
  - Toast dismissal: fade opacity to 0 over 150ms, then remove from tree
  - Command mode expand: panel height transitions 48px → 62px over 150ms (easeOutCubic)
  - Hover states: `panel.bg.elevated` fill fades in over 100ms
  - Lock screen: prompt blurs in (opacity 0→1 over 300ms)
- [ ] Ensure animations don't trigger redraws at full frame rate: only animate-dirty panels render

#### 10.4 Per-output correctness

- [ ] Audit every milestone for output-specific issues:
  - **M4:** Toplevel per-output filtering verified (test with windows on multiple monitors)
  - **M5:** Launcher always appears on the active output, never on a disconnected output
  - **M8:** Tray teleports correctly; no flicker during output switch; no duplicate trays
  - **M9:** Lock surface spans all outputs (compositor handles this via ext-session-lock protocol — verify)
- [ ] Handle output hotplug: new output → create panel → re-layout all panels (tray re-evaluates singleton target)
- [ ] Handle output removal: destroy panel → re-layout remaining panels → if the singleton tray was on the removed output, migrate immediately

#### 10.5 Damage-tracking performance

- [ ] Per-panel dirty rect tracking:
  - `Panel` maintains a dirty rectangle union
  - Widgets set dirty region when their content changes (clock on minute, battery on property change, etc.)
  - `Panel::render()` passes the dirty union to `Renderer`
  - `Renderer` sets `glScissor()` to restrict rendering to dirty region
- [ ] Verify with a "nothing is dirty" frame: `Panel::render()` should be a no-op if dirty rect is empty
- [ ] Verify that the clock ticking once per minute doesn't cause 60fps redraws (check before/after)
- [ ] Memory: check that NanoVG image handles, font atlases, and EGL surfaces are freed on panel destruction

### Deliverables

- Single TOML config file at `~/.config/bezel/config.toml` controls theme, layout, which modules are enabled
- Every visual element reads from theme tokens; user can override any token
- Subtle animations on panel transitions, hover, and notification lifecycle (no jarring instant-swaps)
- No double-rendering on multi-monitor; singleton tray migrates cleanly on focus change
- Hotplug works: connect/disconnect a monitor, panels appear/disappear without crash
- Damage tracking: idle panels render zero GL pixels; active panels render only changed regions

---

## Cross-cutting concerns

### Build system

Each milestone that adds a protocol updates `meson.build` protocols list. Each that adds a dependency (sdbus-c++, nanovg, freetype, harfbuzz, pipewire, tomlplusplus, libpam) adds a `dependency()` call and links it to the `bezel` executable target.

### Include-order hack

The `src/protocol.hpp` namespace macro hack must be examined for each new protocol that goes through `wayland-scanner`. If the generated header doesn't use `namespace` as a parameter name, no hack is needed; include it directly. If it does, extend the hack.

### Error handling pattern

Services that fail to connect (D-Bus unavailable, PipeWire socket missing) should **degrade gracefully** — log the error, mark the service as unavailable, and let the widget tree rebuild without that widget. No crash, no blocking init. This ensures bezel always boots to a functional panel even if optional services are missing.

### Threading

All milestones assume single-threaded event loop. No `std::thread`, no async. If a blocking call is needed (unlikely given all data arrives via D-Bus signals / Wayland events), consider a `fork()` + pipe approach.

---

## Open questions / risks

| # | Question | Milestone | Resolution |
|---|----------|-----------|------------|
| Q1 | MangoWC custom IPC format unknown | M4 | Ship flat-icons fallback first; upgrade to tiling maps when IPC is defined. Do not block M4. |
| Q2 | Does MangoWC expose `wl_event_loop` for fd integration? | M3 | Default to manual `poll()` loop watching Wayland fd + D-Bus fd. More portable. |
| Q3 | Does MangoWC support `xdg-systemtray-v1`? If yes, use it as primary; if not, SNI is primary and `xdg-systemtray-v1` remains an experiment. | M8 | `xdg-systemtray-v1` does not exist in wayland-protocols (checked v1.41 staging). SNI is the sole tray protocol for bezel. Q3 closed. |
| Q4 | ext-session-lock-v1 negotiation — does MangoWC implement it? | M9 | Required protocol; verify compositor support early in M9. If missing, lock is non-functional until compositor adds it. |
| Q5 | PAM configuration (`/etc/pam.d/bezel`) — what service template? | M9 | Ship an example PAM config; user or package manager installs it. Document in AGENTS.md. |
| Q6 | Icon themes for .desktop icons — which icon loader? | M5 | Start with GTK icon theme lookup (`IconThemePath` + `IconName` from .desktop files); use `gtk-icon-theme` headers or parse `index.theme` + `hicolor` manually. |
| Q7 | HiDPI fractional scaling — how to handle non-integer scales? | M2, M10 | EGL surface is sized at integer pixels; `wl_output::scale` tells us the factor. Apply `nvgGlobalScale()` and size widgets in logical pixels. NanoVG handles sub-pixel text. |

---

*(End of milestone tracker)*