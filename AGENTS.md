# AGENTS.md

This file provides guidance to AI coding assistants when working on `bezel`.

## Project overview

`bezel` is a single-process Wayland shell panel written in C++20, designed for
compositors that expose `zwlr_layer_shell_v1` (primarily **MangoWC**). It draws
one panel per output and currently implements workspace switching, a window
list, battery status, clock, and an on-demand launcher.

## Build commands

```bash
meson setup buildDir        # first time (or: meson setup buildDir --reconfigure)
meson compile -C buildDir   # build with Ninja
```

Run the binary directly: `./buildDir/bezel`. Requires a Wayland session with
`zwlr_layer_shell_v1`. GNOME/KDE sessions will fail at init.

There is no test suite and no CI. Verify changes by building and, when
possible, running against the target compositor.

## Architecture

```
App (main.cpp)
  ├── Wayland registry → wl_compositor, zwlr_layer_shell_v1, wl_output, wl_seat,
  │   zwlr_foreign_toplevel_manager_v1, zdwl_ipc_manager_v2
  ├── Egl (shared across all panels)
  │   ├── D-Bus system connection + Services (singletons, owned by App):
  │   │     BatteryService, ToplevelService, WorkspaceService,
  │   │     NetworkService, MprisService, NotificationService   (Service base class)
  │   ├── AudioService (PipeWire), BrightnessService (sysfs+inotify)
  │   ├── OsdOverlay + NotificationOverlay (singleton focus-following surfaces)
  ├── DesktopIndex + SearchEngine (launcher data)
  ├── ControlSocket ($XDG_RUNTIME_DIR/bezel.sock)
  └── Output[] → Panel (one per monitor)
        ├── Renderer (NanoVG / EGL surface)
        ├── FontCache
        └── Widget tree: BoxLayout → [WorkspaceSwitcher, WindowList, Spacer,
                                      BatteryWidget, Clock]
```

**Event loop** (`App::run`): a manual `poll()` loop watches the Wayland fd,
D-Bus fd(s), and the control socket fd simultaneously. All code is
single-threaded — no `std::thread`, no async.

**Services** implement `Service` (see `src/service.hpp`): `init()`,
`tick() → bool`, and an observer pattern via `subscribe(callback)` /
`unsubscribe(id)` / `notify()`. `tick()` returns `true` when state changed.
Services are called each loop iteration from `App::run`.

**Widgets** implement `Widget` (see `src/widget.hpp`):
`preferredWidth/Height()`, `layout(x, y, w, h)`, `render(renderer)`. `BoxLayout`
distributes children; a `Spacer` fills remaining space. Each `Panel` owns a root
`BoxLayout`.

**Rendering**: `Panel::render()` → `Renderer::beginFrame/endFrame()` wrapping
NanoVG draw calls. EGL surfaces are per-panel (`wl_egl_window`). `Panel` has a
`dirty_` flag; `requestRedraw()` sets it. Only dirty panels redraw.

**Theme**: a single `Theme` struct (`src/theme.hpp`) holds all Nocturne
(Tokyo Night-derived) design tokens — colors, font sizes, geometry — and is
passed by reference everywhere. `Theme::defaultTheme()` is the sole source of
truth for defaults.

## Protocol / include-order requirement

`src/protocol.hpp` must include wayland headers in a specific order because the
generated `wlr-layer-shell` header declares a parameter literally named
`namespace` (a C++ keyword):

```cpp
#include <wayland-client.h>
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
```

When adding a new protocol: add its XML to `protocols/`, add an entry to the
`protocols` list in `meson.build`, and add the generated client-protocol header
include to `protocol.hpp` (applying the same `#define` trick only if the
generated header uses `namespace` as a parameter name).

## Key design decisions

- **Single process, one theme.** All panels share one `App`, one `Theme`, one
  set of services. No config-matching across binaries.
- **Graceful degradation.** Services that fail to connect (D-Bus unavailable,
  compositor lacking a protocol) log and disable themselves; the panel still
  boots.
- **Design spec is the reference.** The visual design (Nocturne tokens, panel
  height 48px, layout regions) is in `docs/BEZEL_DESIGN_SPEC.md`. The milestone
  plan is in `docs/milestone-tracker.md`.
- **No manual protocol generation.** `wayland-scanner` runs automatically via
  Meson custom targets. Never manually generate or commit the generated
  `.h`/`.c` files.

## Current milestone status

The source tree implements M1–M7:

- **M1 Foundation** — layer surfaces, one panel per output, EGL context.
- **M2 Render core** — NanoVG + FreeType/HarfBuzz font stack, widget/layout
  system, theme tokens, clock widget.
- **M3 Services + battery** — D-Bus event loop integration, `Service` base
  class, UPower `BatteryService`, demand-driven redraw.
- **M4 Compositor backend** — `ToplevelService` (foreign toplevel),
  `WorkspaceService` (dwl-ipc), `WorkspaceSwitcher`, `WindowList`, per-output
  scoping.
- **M5 Launcher** — `.desktop` parser (`DesktopIndex`), Unix-domain control
  socket, fuzzy search (`SearchEngine`), keyboard-interactive launcher surface
  with app/window/command results.
- **M6 Notifications** — `NotificationService` (org.freedesktop.Notifications
  D-Bus server) + `NotificationOverlay` rendering stacked toasts with timeouts
  and basic actions.
- **M7 OSD + status polish** — `AudioService` (PipeWire) with `VolumeWidget` and volume OSD,
  `BrightnessService` (sysfs + inotify) with brightness OSD, `NetworkService`
  (NetworkManager D-Bus) with `NetworkWidget`, `MprisService` (session D-Bus + libcurl art)
  with `MprisWidget`. Shared `OsdOverlay` surface reuses the M6 notification overlay machinery.

M8–M10 are not yet implemented: system tray (SNI), session lock/power,
config/theming/animation cohesion pass.

Note: `docs/milestone-tracker.md` tracks the current status; M8 is the next
milestone.
