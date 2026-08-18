# M10 Damage-Aware Redraw and Per-Output Correctness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make each Wayland output independently lifecycle-safe and progressively damage-aware without changing NanoVG’s full-frame rendering behavior.

**Architecture:** Add small, testable geometry/damage and output-matching value types, then give each `Panel` ownership of damage and frame scheduling state. `App` will collect output metadata, resolve per-output configuration, and explicitly handle output add/remove/scale changes. Rendering remains full-frame; only Wayland buffer-damage submission and dirty scheduling are optimized.

**Tech Stack:** C++20, Wayland client, EGL/Wayland-EGL, NanoVG, Meson/Ninja, existing `Panel`/`App` architecture.

## Global Constraints

- Keep the single-threaded `poll()` event loop; do not add threads or asynchronous workers.
- Continue rendering complete NanoVG frames; renderer clipping is out of scope.
- Use `wl_surface_damage_buffer` only with buffer-coordinate rectangles; fall back to full-surface damage when uncertain.
- Exact output-name configuration wins over position/primary fallback, which wins over global defaults.
- Output teardown must be idempotent and must not access removed Wayland objects from stale callbacks.
- Preserve existing visual and interaction behavior when damage tracking is disabled or unavailable.
- Every task must leave the tree buildable with `meson compile -C buildDir` when Linux dependencies are available.

## File Map

- Create: `src/damage.hpp` — integer rectangle type and coalescing/scale conversion helpers.
- Create: `src/damage.cpp` — damage helper implementation.
- Create: `src/output_config.hpp` — output identity, selectors, and resolved override model.
- Create: `src/output_config.cpp` — deterministic selector matching and inheritance.
- Modify: `src/panel.hpp` — damage/frame lifecycle interfaces and per-panel output metadata.
- Modify: `src/panel.cpp` — damage accumulation, scale invalidation, frame callback, and safe rendering.
- Modify: `src/main.cpp` — output metadata callbacks, per-output resolution, add/remove/recreate orchestration, and independent scheduling.
- Modify: `meson.build` — compile the new implementation units.
- Create: `tests/damage_test.cpp` — focused helper tests if the project’s test toolchain is available.
- Create: `tests/output_config_test.cpp` — output selector/inheritance tests if the project’s test toolchain is available.

## Interfaces

The following interfaces are shared between tasks:

```cpp
struct DamageRect { int x; int y; int width; int height; };
std::vector<DamageRect> coalesceDamage(std::vector<DamageRect> rects,
                                       int surfaceWidth, int surfaceHeight);
DamageRect scaleDamage(DamageRect logical, int scale,
                       int bufferWidth, int bufferHeight);

struct OutputIdentity {
    std::string name;
    int32_t x = 0;
    int32_t y = 0;
    bool primary = false;
};
struct OutputSelector {
    std::optional<std::string> name;
    std::optional<int32_t> x;
    std::optional<int32_t> y;
    std::optional<bool> primary;
};
struct OutputConfig {
    bool panelVisible = true;
    int panelHeight = 48;
    bool launcherEnabled = true;
    bool trayEnabled = true;
    bool notificationsEnabled = true;
    bool workspacesEnabled = true;
    bool windowsEnabled = true;
    bool lockOnIdle = true;
    std::string themePath;
};
struct OutputOverride {
    std::optional<bool> panelVisible;
    std::optional<int> panelHeight;
    std::optional<bool> launcherEnabled;
    std::optional<bool> trayEnabled;
    std::optional<bool> notificationsEnabled;
    std::optional<bool> workspacesEnabled;
    std::optional<bool> windowsEnabled;
    std::optional<bool> lockOnIdle;
    std::optional<std::string> themePath;
};
OutputConfig resolveOutputConfig(
    const OutputIdentity&, const OutputConfig& global,
    const std::vector<std::pair<OutputSelector, OutputOverride>>&,
    std::function<void(std::string_view)> onConflict = {});
```

### Task 1: Add damage geometry primitives

**Files:**
- Create: `src/damage.hpp`
- Create: `src/damage.cpp`
- Create: `tests/damage_test.cpp`
- Modify: `meson.build`

- [ ] **Step 1: Define the value type and helper contracts**

Declare `DamageRect` with an `empty()` predicate and functions `coalesceDamage`, `scaleDamage`, and `fullDamage`. Rectangles use half-open coordinates (`x <= p.x < x + width`).

- [ ] **Step 2: Write focused failing tests**

```cpp
TEST_CASE("damage clips rectangles to the surface") {
    auto out = coalesceDamage({{-4, 2, 10, 8}}, 100, 48);
    REQUIRE(out == std::vector<DamageRect>{{0, 2, 6, 8}});
}
TEST_CASE("overlapping damage coalesces") {
    auto out = coalesceDamage({{0, 0, 10, 10}, {8, 8, 10, 10}}, 100, 48);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0] == DamageRect{0, 0, 18, 18});
}
TEST_CASE("logical damage converts conservatively to buffer pixels") {
    REQUIRE(scaleDamage({1, 2, 3, 4}, 2, 20, 20) == DamageRect{2, 4, 6, 8});
}
```

The repository currently has no test framework or test target. Add a small standalone executable target only if dependency-free `assert` cases can be integrated without changing the application target; otherwise compile the helper source with a temporary local command and record the exact command/output in the task verification notes. Do not add a new third-party test dependency.

- [ ] **Step 3: Implement clipping, coalescing, and scale conversion**

Clip invalid/out-of-bounds rectangles, merge intersecting or touching rectangles, cap the result at a safe fixed count (falling back to one full rectangle), and clamp scaled coordinates to buffer bounds.

- [ ] **Step 4: Run the focused test and build**

Run: `meson setup buildDir` (or `meson setup buildDir --reconfigure`), then `meson compile -C buildDir` and the damage test executable. Expected: all damage cases pass and the application compiles.

- [ ] **Step 5: Commit**

```bash
git add src/damage.hpp src/damage.cpp tests/damage_test.cpp meson.build
git commit -m "feat: add damage rectangle primitives"
```

### Task 2: Add output identity and resolved configuration matching

**Files:**
- Create: `src/output_config.hpp`
- Create: `src/output_config.cpp`
- Create: `tests/output_config_test.cpp`
- Modify: `meson.build`

- [ ] **Step 1: Define selectors and optional overrides**

Represent selectors as optional exact `name`, optional `x`/`y`, and optional `primary`; represent overrides with `std::optional` values so unspecified fields inherit. The initial concrete fields are panel visibility/height, launcher/tray/notification/workspace/window enablement, lock-on-idle, and a theme path. Keep the model extensible for later configuration slices rather than inventing fields that the current config system cannot consume.

- [ ] **Step 2: Write precedence and inheritance tests**

```cpp
TEST_CASE("exact name beats position and primary") {
    OutputIdentity id{"DP-1", 1920, 0, true};
    auto result = resolveOutputConfig(id, global,
        {{OutputSelector{.primary=true}, primary},
         {OutputSelector{.x=1920, .y=0}, position},
         {OutputSelector{.name="DP-1"}, named}});
    REQUIRE(result.panelHeight == named.panelHeight);
}
TEST_CASE("unspecified output fields inherit global values") {
    auto result = resolveOutputConfig({"HDMI-A-1", 0, 0, false}, global,
        {{OutputSelector{.name="HDMI-A-1"}, OutputOverride{.panelVisible=false}}});
    REQUIRE_FALSE(result.panelVisible);
    REQUIRE(result.panelHeight == global.panelHeight);
}
```

- [ ] **Step 3: Implement deterministic resolution and conflict diagnostics**

Apply all matching selectors in specificity order: global, primary/position fallback, then exact name. Within the same specificity, preserve config order and emit a diagnostic callback when two selectors set the same field. Never leave an unresolved field uninitialized.

- [ ] **Step 4: Run tests and compile**

Run the output-config test and `meson compile -C buildDir`. Expected: precedence, inheritance, and conflict tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/output_config.hpp src/output_config.cpp tests/output_config_test.cpp meson.build
git commit -m "feat: resolve per-output configuration"
```

### Task 3: Give Panel independent damage and frame state

**Files:**
- Modify: `src/panel.hpp`
- Modify: `src/panel.cpp`

- [ ] **Step 1: Add panel lifecycle interfaces**

Add `setOutputIdentity(OutputIdentity)`, `setScale(int)`, `requestRedraw(DamageRect)`, `requestFullRedraw()`, `handleFrameDone(uint32_t)`, `invalidateSurface()`, and `outputIdentity()`. Store `std::vector<DamageRect> damage_`, `bool framePending_`, `bool fullDamage_`, and a generation counter used to reject stale callbacks.

- [ ] **Step 2: Make redraw requests accumulate damage**

Keep `requestRedraw()` as a full-damage compatibility wrapper. Rectangular requests append logical coordinates, set `dirty_`, and never modify another panel. Scale changes, resize, launcher geometry changes, and surface invalidation call `requestFullRedraw()`.

- [ ] **Step 3: Add per-surface frame callbacks**

Install a `wl_callback` after each committed frame with a listener carrying the panel generation. On callback, clear `framePending_` and render only if `dirty_` or an active owner requires it. Destroy/invalidate the callback before panel destruction or surface recreation.

- [ ] **Step 4: Submit damage safely after full-frame rendering**

Before `eglSwapBuffers`, coalesce logical rectangles, convert them with the current scale, and call `wl_surface_damage_buffer` for each rectangle, or call one full-buffer rectangle when the list is empty/invalid/too large. Let `eglSwapBuffers` attach and commit the rendered buffer as it does today; install the frame callback before the swap so the callback is associated with that commit. Treat the first configured frame as full damage.

- [ ] **Step 5: Validate panel behavior**

Build the application and manually check that a clock update dirties one panel, a scale change causes a full redraw, and no frame callback remains after a panel is destroyed. Record any compositor-specific damage API limitation in the commit message rather than changing to renderer clipping.

- [ ] **Step 6: Commit**

```bash
git add src/panel.hpp src/panel.cpp
git commit -m "feat: track panel damage and frame callbacks"
```

### Task 4: Track output metadata and create panels from resolved output state

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/panel.hpp`
- Modify: `src/panel.cpp`

- [ ] **Step 1: Extend `Output` metadata**

Store geometry position, description-derived fallback identity, primary flag, output configuration snapshot, and a lifecycle generation. Replace the no-op geometry callback with one that records `x` and `y`; update scale and name callbacks to mark the owning output changed.

- [ ] **Step 2: Resolve configuration before first panel creation**

In `create_panel(Output&)`, compute the `OutputIdentity`, resolve its output configuration, pass the resulting panel geometry/visibility and behavior settings into `Panel`, then set identity and scale before the first commit/render. If the output is hidden by configuration, keep its metadata but do not create a layer surface.

- [ ] **Step 3: Preserve output-local state**

Ensure workspace, window, tray, notification, launcher, and power-menu callbacks capture the owning `Panel`/`Output` rather than a global “active output” for rendering decisions. A service update must call `requestRedraw` only on panels whose output scope is affected.

- [ ] **Step 4: Reconfigure on output `done`**

When output metadata changes, compare identity/scale/configuration with the stored snapshot. Recreate only that panel if surface geometry or visibility requires it; otherwise apply scale/layout changes in place and request full damage. Do not rebuild unrelated panels.

- [ ] **Step 5: Build and manually verify initial multi-output setup**

Run `meson compile -C buildDir`; under a Wayland compositor with two outputs, verify both panels are created with independent names, scales, and redraw state.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/panel.hpp src/panel.cpp
git commit -m "feat: resolve panel state per output"
```

### Task 5: Make output removal, recreation, and hot-plug safe

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/panel.cpp`

- [ ] **Step 1: Add idempotent output teardown**

Create an `App::removeOutput(Output&)` helper that clears hovered pointers, tray ownership, lock overlays, popup/transient references, animation registrations, and frame callbacks before destroying the panel and removing the `wl_output` proxy. Calling it twice must be harmless.

- [ ] **Step 2: Handle registry removal through the helper**

Update `reg_global_remove` to locate by registry name, call `removeOutput`, and erase only that output. Ensure no later service callback dereferences the removed `Output` or `Panel`.

- [ ] **Step 3: Recreate only the affected output on scale/geometry changes**

Add `recreateOutput(Output&)`: destroy its panel and dependent output-scoped surfaces, refresh scale/geometry/config, create a new panel, and request full damage. Keep other `Output` objects and their panels untouched. If recreation fails, log it and leave healthy outputs alive.

- [ ] **Step 4: Guard stale callbacks and invalid scales**

Use the panel generation token in frame callbacks and clamp non-positive scale values to 1 while logging the invalid event. A stale callback must only release its callback proxy and return; it must not render or touch freed state.

- [ ] **Step 5: Exercise lifecycle validation**

Run the build and manually test output hot-plug, unplug, scale changes, panel surface recreation, lock/notification/OSD/tray surfaces, and workspace/window scoping. Expected: no crash, no stale callback access, and no unrelated output repaint/recreation.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/panel.cpp
git commit -m "fix: isolate output lifecycle and teardown"
```

### Task 6: Integrate dirty scheduling and complete verification

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/panel.hpp`
- Modify: `src/panel.cpp`
- Modify: `docs/milestone-tracker.md`

- [ ] **Step 1: Separate tick, animation, and dirty work per panel**

Change the event-loop tick path so `Panel::tick()` updates only its own state and `Panel::render()` is attempted only when that panel is dirty, has pending animation/overlay work, or has no frame callback and needs its first frame. Never use a global dirty flag or global frame callback.

- [ ] **Step 2: Wire overlay and animation ownership boundaries**

When an output-owned overlay or animation changes, call that panel/surface’s redraw request. On completion, unregister only that owner. Shared overlays must identify their target output explicitly; they must not keep every panel continuously dirty.

- [ ] **Step 3: Add safe full-damage fallback and diagnostics**

Log unknown output identity, unavailable damage support, inconsistent geometry, stale callback, and failed recreation once per event. Use full-surface damage and retain the last valid surface whenever a precise update cannot be trusted.

- [ ] **Step 4: Run final verification**

Run:

```bash
meson setup buildDir --reconfigure
meson compile -C buildDir
```

Then perform the documented multi-monitor manual checks: hot-plug/unplug, scale changes, per-output overrides, launcher, workspace/window list, tray, notifications, OSD, animations, and lock surfaces. Expected: clean build and independent output behavior; renderer clipping is not required.

- [ ] **Step 5: Update milestone tracking and inspect the diff**

Mark only this M10 slice complete in `docs/milestone-tracker.md`, run `git diff --check`, and inspect `git diff` for accidental unrelated changes.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/panel.hpp src/panel.cpp docs/milestone-tracker.md
git commit -m "feat: complete damage-aware output scheduling"
```
