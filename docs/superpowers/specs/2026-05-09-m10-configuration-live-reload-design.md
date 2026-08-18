# M10 Configuration and Live Reload Design

**Date:** 2026-05-09  
**Status:** Approved design

## Goal

Add a central, typed configuration system for the current M1–M9 functionality. It will cover module enablement, appearance/layout, launcher behavior, keybindings, and session settings, with safe live reload. Animation and broader rendering optimization remain separate M10 slices.

## Configuration model and ownership

A central `Config` model owns typed defaults, parsing, and validation. Focused sections are:

- `modules`: enable/disable clock, battery, network, volume, MPRIS, tray, notifications, workspaces, windows, launcher, and power controls.
- `theme`: colors, typography, spacing, panel geometry, and overlay styling.
- `layout`: widget ordering, visibility, alignment, and per-region sizing.
- `launcher`: search paths, result limits, terminal command, desktop-entry behavior, and activation preferences.
- `keybindings`: global actions such as launcher, lock, power menu, reload, and module-specific controls.
- `session`: idle timeout, lock-on-idle, lock mode, and related M9 settings.
- `logging`: log-file location and minimum severity.

Services and widgets do not parse TOML directly. They consume typed values through `App`; reload orchestration decides whether a component is updated in place, rebuilt, or deferred.

## Loading and diagnostics

Configuration path resolution is:

1. `$BEZEL_CONFIG`, when set
2. `$XDG_CONFIG_HOME/bezel/config.toml`
3. `~/.config/bezel/config.toml`
4. Built-in defaults when no file exists

A `ConfigLoader` produces structured diagnostics containing severity, source path, section/key, and message. Valid fields are applied independently. Invalid syntax or types are logged and ignored; during reload, the previous value is retained, while startup falls back to its built-in default. Unknown keys are warned about and ignored. Values use safe bounds for dimensions, timeouts, result counts, and similar stability-sensitive fields.

Diagnostics are written to stderr and a log file under `$XDG_STATE_HOME/bezel/`, falling back to `~/.local/state/bezel/`. Configurable severity and rotation limits prevent unbounded growth.

## Reload behavior

Reload is triggered by both `SIGHUP` and the `reload` control-socket command. The signal handler only sets a flag or wakes the existing event loop; parsing runs in the normal loop.

The loader compares old and new typed configurations and emits a `ConfigDiff` classifying changes as in-place, rebuild, or deferred. The app applies changes deterministically:

1. Logging and diagnostics
2. Keybindings and launcher settings
3. Theme/layout and affected panel rebuilds
4. Service settings and module enablement
5. Session/idle settings

Safe changes apply immediately. Changes requiring Wayland object recreation are deferred or rebuild only the affected component. Active locks, notifications, OSDs, menus, and other transient state are preserved when safe; otherwise only the affected transient surface may close.

Keybindings, theme/layout, and service settings apply immediately where supported. Only changes requiring unsafe Wayland object recreation are deferred or rebuilt, preserving the process and unaffected state.

## Implementation boundaries

The implementation consists of:

- `Config` and section structs for typed defaults and validation.
- `ConfigLoader` for path resolution, TOML parsing, diagnostics, and log output.
- `ConfigDiff` for change detection and update classification.
- An `App` reload coordinator.
- Narrow reconfigure/update methods on existing services and widgets.

The schema will cover current M1–M9 behavior while allowing later animation settings to be added without redesigning the loader.

## Testing and validation

Tests will cover path precedence, defaults, valid and invalid field handling, unknown-key diagnostics, range validation, partial application, diff classification, and reload behavior. Meson build validation will be used. Runtime checks will cover `SIGHUP`, the socket command, missing files, malformed TOML, and log-file fallback.

Animation and broader damage-aware rendering/per-output optimization are explicitly out of scope for this slice.
