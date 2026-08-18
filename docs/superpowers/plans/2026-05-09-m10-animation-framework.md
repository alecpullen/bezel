# M10 Animation Framework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a centralized, single-threaded animation controller and integrate smooth, interruptible transitions for panel hover/launcher state, notifications, and lock-screen prompt appearance.

**Architecture:** `App` owns an `AnimationController` that advances monotonic-time animations from the existing event loop. The controller provides scalar interpolation, easing, retargeting, cancellation, reduced-motion policy, and redraw ownership without knowing widget rendering details. Widgets and overlays retain semantic state and expose animation values used by their existing render paths.

**Tech Stack:** C++20, Meson/Ninja, NanoVG, Wayland layer-shell surfaces, existing single-threaded `poll()` loop, `std::chrono::steady_clock`.

## Global Constraints

- No threads, asynchronous timers, or second event loops.
- Elapsed time uses `std::chrono::steady_clock`; negative elapsed time is zero and large deltas are clamped.
- Zero-duration or disabled animations apply final values immediately.
- Retargetable animations start from their current interpolated value.
- Completion callbacks run at most once; invalidated owners cancel callbacks safely.
- Reduced-motion precedence is explicit Bezel override, then desktop preference, then normal durations.
- Existing behavior and interaction semantics remain available when animations are disabled.
- Theme/configuration parsing is out of scope; consumers receive duration and motion-policy values through narrow interfaces.

## File Map

- Create `src/animation.hpp` and `src/animation.cpp`: easing/interpolation primitives, redraw-owner handle, animation records, and `AnimationController`.
- Create `tests/animation_test.cpp`: deterministic unit tests for timing, interpolation, policy, retargeting, cancellation, and callback cardinality.
- Modify `meson.build`: compile the controller and add a native test target using the project’s available test framework/tooling.
- Modify `src/main.cpp`: own/advance the controller, include animation-driven poll timeout, and pass animation policy to consumers.
- Modify `src/panel.hpp`/`src/panel.cpp`: add hover opacity and launcher expansion state, animation callbacks, and redraw ownership.
- Modify: `src/context_menu_surface.hpp`/`.cpp`, `src/power_menu.hpp`/`.cpp`, and `src/power_button_widget.hpp`/`.cpp` only where their existing hover highlight is drawn; each change is limited to a hover-opacity value and redraw callback. Workspace/window widgets have no independent hover transition and remain unchanged.
- Modify `src/notification_overlay.hpp`/`src/notification_overlay.cpp` and `src/notification_service.*`: track toast entry/exit animation state, commit dismissal once, defer removal until exit completion, and repaint while animated.
- Modify `src/lock_overlay.hpp`/`src/lock_overlay.cpp`: add prompt opacity and use it when drawing the lock prompt.
- `src/theme.hpp` is intentionally unchanged in this slice; callers use fixed defaults (`hover=120ms`, `launcher=180ms`, `toast=180ms`, `dismissal=160ms`, `lock prompt=220ms`) until the theming/configuration slices expose them.

### Task 1: Add deterministic animation primitives and controller

**Files:**
- Create: `src/animation.hpp`
- Create: `src/animation.cpp`
- Create: `tests/animation_test.cpp`
- Modify: `meson.build`

**Interfaces:**
- Produces `enum class Easing { Linear, EaseInOut }`.
- Produces `struct AnimationPolicy { bool enabled; bool reducedMotion; bool forceMotion; std::chrono::milliseconds reducedDuration; }`.
- Produces `class AnimationController` with `using Id = uint64_t`, `using Owner = const void*`, and methods:
  - `Id animate(Owner owner, float from, float to, milliseconds duration, Easing easing, std::function<void()> redraw, std::function<void(float)> update, std::function<void()> complete = {})`.
  - `bool retarget(Id id, float to, milliseconds duration, Easing easing)`.
  - `void cancel(Id id)` and `void cancelOwner(const void* owner)`.
  - `bool advance(steady_clock::time_point now)`.
  - `bool active() const`.
  - `void setPolicy(AnimationPolicy policy)`.
- Each animation records its owner identity separately from callbacks so invalidation cancels safely; completion is delivered once.

- [ ] **Step 1: Write failing unit tests** for linear/ease-in-out endpoints, zero duration, negative time, maximum-delta clamping, retargeting from current value, cancellation, reduced-motion policy, redraw calls while active, and exactly-once completion.
- [ ] **Step 2: Run the focused test target** and verify the new tests fail because the controller is absent.
- [ ] **Step 3: Implement the minimal controller** with a configurable maximum delta (default 100ms), monotonic timestamps, final-value application for disabled/zero-duration animations, and stable IDs.
- [ ] **Step 4: Run the focused tests** and verify all primitives pass.
- [ ] **Step 5: Add the source and test target to Meson** without adding a new runtime dependency; use the repository’s existing C++ test convention or a small standalone executable if no framework exists.
- [ ] **Step 6: Commit** with `feat: add centralized animation controller`.

### Task 2: Integrate controller scheduling into App

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/animation.hpp` only if the event-loop API needs a small correction.

**Interfaces:**
- `App` owns `AnimationController animations_` and exposes a private `requestAnimationRedraw()` callback used by surfaces.
- `App::tick()` advances animations once per loop using `steady_clock::now()`.

- [ ] **Step 1: Add the controller member and initialize its policy** with animations enabled and the default reduced-motion setting, leaving a later config/theme hook.
- [ ] **Step 2: Change the poll timeout** so active animations wake the loop at approximately 16ms and inactive animations retain existing service/bus timeouts.
- [ ] **Step 3: Advance animations after event/service processing and before rendering**, ensuring callbacks only touch live panels/overlays.
- [ ] **Step 4: Ensure active animation redraw callbacks call `requestRedraw()` or set an overlay dirty flag**, and that no redraw callback runs after its owner has been destroyed.
- [ ] **Step 5: Build and run the focused animation tests**.
- [ ] **Step 6: Commit** with `feat: schedule animations from app event loop`.

### Task 3: Add panel hover fades and launcher expansion animation

**Files:**
- Modify: `src/panel.hpp`
- Modify: `src/panel.cpp`
- Modify: the specific hoverable widget headers/sources identified by the source audit (for example workspace/window/power widgets), keeping each change limited to a hover-opacity value and redraw callback.

**Interfaces:**
- `Panel` accepts an `AnimationController*` and an animation-duration/policy provider without parsing config.
- Hoverable components expose `setHoverProgress(float)` or equivalent and render their existing hover tint multiplied by that progress.
- Launcher expansion uses `float launcherProgress_` in `[0,1]`; launcher show targets 1 and dismissal targets 0.

- [ ] **Step 1: Audit pointer enter/motion/leave paths** and identify every visual hover literal; document intentional non-animated interaction feedback separately.
- [ ] **Step 2: Add failing tests or deterministic helper assertions** for hover progress endpoints and launcher progress retargeting.
- [ ] **Step 3: Add panel-owned animation IDs** that retarget from current progress on repeated enter/leave and cancel safely when the panel is destroyed.
- [ ] **Step 4: Replace hover redraw-only transitions** with controller updates while preserving hit testing and click behavior.
- [ ] **Step 5: Animate launcher expansion/collapse** while keeping keyboard focus and surface size semantics unchanged; apply final state immediately when motion is disabled.
- [ ] **Step 6: Build, run focused tests, and manually inspect panel hover and launcher behavior where a Wayland session is available.**
- [ ] **Step 7: Commit** with `feat: animate panel hover and launcher transitions`.

### Task 4: Animate notification entry, expansion, and dismissal

**Files:**
- Modify: `src/notification_service.hpp`
- Modify: `src/notification_service.cpp`
- Modify: `src/notification_overlay.hpp`
- Modify: `src/notification_overlay.cpp`
- Modify: `src/main.cpp` only if App must inject the controller into the overlay.

**Interfaces:**
- `NotificationService` exposes a dismissal-commit operation that marks an ID as committed and ignores duplicate requests.
- `NotificationOverlay` receives `AnimationController&`, owns per-toast opacity/offset animation IDs, and marks itself dirty from animation callbacks.

- [ ] **Step 1: Add tests for duplicate dismissal and deferred removal**, asserting that the notification remains available during exit and is removed exactly once after completion.
- [ ] **Step 2: Implement notification lifecycle flags** (`entering`, `dismissalCommitted`, and pending-removal state) without changing D-Bus reason codes.
- [ ] **Step 3: Start entry animations for new notifications** and animate toast opacity/position in the overlay; retain current static layout when disabled.
- [ ] **Step 4: Animate hover-driven expansion** using a retargetable progress value rather than switching layout instantly; use the current interpolated value as the next animation’s start.
- [ ] **Step 5: Start dismissal fade once, ignore repeated close requests, and remove the toast only from the completion callback**; cancel safely if the overlay surface is invalidated.
- [ ] **Step 6: Ensure overlay geometry and rendering remain valid while animated** and redraw only while dirty or an animation is active.
- [ ] **Step 7: Build, run tests, and manually verify notification entry, hover expansion, click dismissal, and timeout dismissal.**
- [ ] **Step 8: Commit** with `feat: animate notification transitions`.

### Task 5: Animate lock-screen prompt fade-in

**Files:**
- Modify: `src/lock_overlay.hpp`
- Modify: `src/lock_overlay.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- `LockOverlay` receives an `AnimationController*` and owns a prompt-opacity animation scoped to the overlay surface.
- `LockOverlay::render(const LockViewState&)` draws prompt-related content using the current opacity while preserving the opaque lock background and input behavior.

- [ ] **Step 1: Add a deterministic test/helper assertion** that prompt opacity is 0 before the fade and 1 after completion, with immediate 1 for disabled motion.
- [ ] **Step 2: Start the prompt fade when a lock overlay becomes visible/configured**, not on every frame.
- [ ] **Step 3: Apply opacity only to prompt text/input decoration**, never to the full opaque lock background or security-critical input state.
- [ ] **Step 4: Cancel the animation on hide/destruction and prevent callbacks from touching invalid renderer state.**
- [ ] **Step 5: Build and manually verify hard-lock and soft-lock prompt appearance, authentication, and reduced-motion behavior.**
- [ ] **Step 6: Commit** with `feat: fade in lock prompt`.

### Task 6: Full verification and documentation

**Files:**
- Modify: `docs/milestone-tracker.md` only if the existing M10 tracking has a specific animation entry requiring status.
- Modify: `README.md` only if an animation runtime/configuration note is required by existing documentation conventions.

- [ ] **Step 1: Run the complete Meson build** with `meson compile -C buildDir` and record any environment-only dependency failures without masking source errors.
- [ ] **Step 2: Run the complete animation unit test target** and verify timing, policy, lifecycle, and integration helper coverage.
- [ ] **Step 3: Review the diff for forbidden second clocks/threads, unsafe callbacks, unbounded animation growth, and accidental interaction changes.**
- [ ] **Step 4: Perform manual Wayland checks** for hover, launcher, notifications, lock prompt, animation disablement, reduced-motion preference, and output/surface teardown.
- [ ] **Step 5: Update documentation only for behavior actually implemented.**
- [ ] **Step 6: Commit** with `docs: document animation framework validation` if documentation changed; otherwise retain the prior task commits.
