# M10 Animation Framework Design

## Goal

Add a centralized, single-threaded animation framework and integrate it with the specified M10 visual effects without changing interaction semantics or requiring threads.

## Scope

This slice includes:

- A central `AnimationController` owned by `App`.
- Monotonic elapsed-time animation with clamped deltas.
- Typed interpolation and easing primitives.
- Retargeting from the current interpolated value.
- Reduced-motion handling from desktop preference plus an explicit Bezel override.
- Dirty-panel/surface scheduling only while animations are active.
- Hover fades, command-mode expansion, toast entry/exit transitions, toast dismissal fading, and lock-screen prompt fade-in.

It excludes theme parsing, visual redesign, and damage-region optimization. Animation durations and reduced-motion settings are consumed through a narrow interface supplied by the theme/configuration work.

## Architecture and ownership

`App` owns one `AnimationController` and advances it from the existing event loop using a monotonic clock. No threads, asynchronous timers, or independent widget clocks are introduced.

Each animation contains a start value, target value, duration, easing function, current interpolated value, completion state, update callback or typed property binding, and an owning panel or surface for redraw scheduling. Widgets and overlays retain semantic state; the controller owns timing and interpolation. Renderers draw the current values only.

The controller clamps elapsed deltas after event-loop stalls. A zero-duration or disabled animation applies its final value immediately. Normal animations use configured durations unless reduced-motion policy applies. Policy precedence is:

1. Explicit Bezel override, if configured.
2. Desktop reduced-motion preference.
3. Normal configured durations.

Reduced-motion disables animations or uses a minimal transition duration according to the configuration contract; it never leaves a visual effect in an indeterminate state.

## Interruption and lifecycle rules

Retargetable state animations—hover opacity, command-mode expansion, and prompt opacity—replace the prior target and start from the currently interpolated value.

One-shot toast dismissal animations become committed when dismissal starts. Duplicate dismissal requests are ignored, and the toast is removed only after its exit animation completes. Surface teardown is deferred until an exit animation completes unless the surface is explicitly cancelled or invalidated.

Each active animation schedules redraws for its owning panel or surface. When no animations remain, the controller stops requesting animation-driven redraws and existing dirty-state behavior resumes.

## Planned integrations

- Hover fades interpolate widget hover opacity.
- Command-mode expansion interpolates its expansion/progress value.
- Toast entry and exit transitions animate opacity/position as appropriate.
- Toast dismissal fades before removal and cannot be requeued after commitment.
- Lock-screen prompt fade-in animates prompt opacity.

Every integration preserves the current immediate final state when animation is disabled.

## Interfaces

The implementation will provide a narrow controller API for starting, retargeting, cancelling, and advancing animations, plus a way to associate redraw ownership with a panel or overlay. Easing and interpolation primitives will be independently testable. Consumers will supply duration and reduced-motion policy without parsing configuration or theme files themselves.

## Error and edge handling

Invalid or missing durations resolve to safe defaults supplied by the caller. Negative elapsed time is treated as zero. Large elapsed intervals are clamped to the controller's maximum delta. Invalidated owners cancel their animations without invoking callbacks against destroyed surfaces. Completion callbacks are invoked at most once.

## Testing and validation

Unit tests will cover easing boundaries, interpolation, zero-duration behavior, reduced-motion behavior, delta clamping, retargeting, cancellation, completion callback cardinality, duplicate toast dismissal, and redraw scheduling. Integration checks will exercise all planned effects and verify that disabling animation applies final state immediately.

Validation will use the existing Meson build and manual runtime checks for panel hover, command mode, notifications, and lock-screen prompt behavior. The implementation will not add a second timing loop or threads.

## Compatibility

Existing visual behavior remains available through disabled animations or zero-duration settings. The design introduces no new protocol dependency and does not alter lock, notification, launcher, or input semantics.
