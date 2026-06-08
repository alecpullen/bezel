# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

```bash
meson setup buildDir        # first time (or: meson setup buildDir --reconfigure)
meson compile -C buildDir   # build (runs Ninja)
```

Artifacts land in `buildDir/`. Run the binary directly: `./buildDir/myshell`. Requires a Wayland session with `zwlr_layer_shell_v1` — the expected compositor is **MangoWC**. GNOME/KDE sessions will fail at init.

There is no test suite and no CI. Verify changes by building and running against the compositor.

## Architecture

Single-process C++20 Wayland shell panel. The high-level flow:

```
App (main.cpp)
  ├── Wayland registry → wl_compositor, zwlr_layer_shell_v1, wl_output, protocol managers
  ├── Egl (shared across all panels)
  ├── Services (singletons, owned by App):
  │     BatteryService, ToplevelService, WorkspaceService   (Service base class)
  └── Output[] → Panel (one per monitor)
        ├── Renderer (NanoVG / EGL surface)
        ├── FontCache
        └── Widget tree: BoxLayout → [WorkspaceSwitcher, ..., Clock, BatteryWidget]
```

**Event loop** (`App::run`): a manual `poll()` loop that watches the Wayland fd and the D-Bus fd simultaneously. All code is single-threaded — no `std::thread`, no async.

**Services** implement `Service` (see `src/service.hpp`): `init()`, `tick() → bool`, and an observer pattern via `subscribe(callback)` / `unsubscribe(id)` / `notify()`. `tick()` returns `true` when state changed. Services are called each loop iteration from `App::run`.

**Widgets** implement `Widget` (see `src/widget.hpp`): `preferredWidth/Height()`, `layout(x, y, w, h)`, `render(renderer)`. `BoxLayout` distributes children; a `Spacer` fills remaining space. Each `Panel` owns a root `BoxLayout`.

**Rendering**: `Panel::render()` → `Renderer::beginFrame/endFrame()` wrapping NanoVG draw calls. EGL surfaces are per-panel (`wl_egl_window`). `Panel` has a `dirty_` flag; `requestRedraw()` sets it. Only dirty panels redraw.

**Theme**: a single `Theme` struct (`src/theme.hpp`) holds all Nocturne (Tokyo Night-derived) design tokens — colors, font sizes, geometry — and is passed by reference everywhere. `Theme::defaultTheme()` is the sole source of truth for defaults.

## Protocol / include-order requirement

`src/protocol.hpp` must include wayland headers in a specific order because the generated `wlr-layer-shell` header declares a parameter literally named `namespace` (a C++ keyword):

```cpp
#include <wayland-client.h>
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
```

When adding a new protocol: add its XML to `protocols/`, add an entry to the `protocols` list in `meson.build`, and add the generated client-protocol header include to `protocol.hpp` (applying the same `#define` trick only if the generated header uses `namespace` as a parameter name).

## Key design decisions

- **Single process, one theme.** All panels share one `App`, one `Theme`, one set of services. No config-matching across binaries.
- **Graceful degradation.** Services that fail to connect (D-Bus unavailable, compositor lacking a protocol) log and disable themselves; the panel still boots.
- **Design spec is the reference.** The visual design (Nocturne tokens, panel height 48px, layout regions) is in `docs/MYSHELL_DESIGN_SPEC.md`. The milestone plan is in `docs/milestone-tracker.md`.
- **No manual protocol generation.** `wayland-scanner` runs automatically via Meson custom targets. Never manually generate or commit the generated `.h`/`.c` files.

## Current milestone status (as of M4)

M4 (Compositor backend) is partially landed: `ToplevelService` and `WorkspaceService` are implemented; `WorkspaceSwitcher` widget exists. The window list widget (`WindowList`) is not yet implemented. See `docs/milestone-tracker.md` for the full backlog.
