# M10 Theming and Appearance Cohesion Design

## Goal

Make Bezel's visual appearance fully token-driven and reloadable, while supporting best-effort external TOML themes with custom font and icon assets.

## Scope

This slice covers visual-token centralization, external theme loading, asset lookup/fallback, and interfaces for runtime theme refresh. It preserves the existing Nocturne appearance and interaction semantics. Configuration path/reload orchestration, animation, and damage optimization remain separate slices.

## Theme model

`Theme` is the single source of visual defaults and runtime values. It contains named tokens for every visual value used by panels, widgets, launcher, lock/power UI, notification and OSD overlays, tray and tooltip surfaces, typography, spacing, geometry, opacity, and icon presentation. `Theme::defaultTheme()` remains the canonical source of the current Nocturne defaults; the cohesion pass maps existing visual literals to equivalent named tokens without changing the default appearance.

Theme values include:

- Colors, including alpha/opacity where applicable.
- Font family/file selections and fallback font files.
- Font sizes and text weights.
- Spacing, corner radii, borders, shadows, panel and overlay dimensions.
- Widget and overlay geometry, including tooltip and launcher styling.
- Icon theme/path selections and icon sizing/tint values.

Protocol-required values and non-visual algorithmic constants remain outside the theme. Widgets, panels, renderers, and overlays consume the typed theme by reference and never parse TOML or resolve asset paths.

## External themes and overrides

A selected external TOML theme file supplies theme values, and inline theme values in the main configuration override values supplied by that file. The loader applies values independently using best-effort semantics:

- Valid known values are applied.
- Invalid syntax or types are logged and leave the previous value unchanged during reload, or the built-in default on startup.
- Unknown keys are logged and ignored.
- Missing values use the selected prior/default value.

The theme loader produces structured diagnostics containing severity, source path, section/key, and a human-readable message. Diagnostics use the configuration system's stderr and `$XDG_STATE_HOME/bezel/` (falling back to `~/.local/state/bezel/`) logging facilities; this slice defines the interface but does not duplicate configuration reload orchestration.

## Asset resolution

User-supplied font and icon paths resolve in this order:

1. Explicit absolute paths.
2. Relative paths resolved against the external theme file's directory.
3. `$XDG_DATA_HOME/bezel/themes`.
4. System theme directories.

The exact system search directories follow the platform's XDG data-directory conventions. Unavailable or malformed assets never prevent Bezel from rendering: the loader reports a diagnostic and retains the existing resource or falls back to the built-in font/icon behavior.

## Runtime reload behavior

`ThemeLoader` returns a typed theme update and a change classification. `App` owns coordination with the existing configuration reload mechanism. Token-only changes update the shared theme and request repaint of all panels and overlays in place. Font changes refresh only the affected `FontCache` registrations; icon changes refresh only the affected icon resources. Existing transient state is preserved whenever its dependencies can be updated safely. If a resource cannot be replaced safely in place, only the affected visual component is rebuilt or its transient surface is closed; the process and unrelated surfaces continue running.

Font ownership and fallback remain in `FontCache`, including safe replacement of loaded faces and fallback selection. Icon lookup and placeholder fallback remain in the icon-rendering path. Resource replacement must not leave widgets holding dangling references; updates happen at an explicit refresh boundary before repaint.

## Component boundaries

- `Theme`: typed token values, built-in defaults, validation, and change classification.
- `ThemeLoader`: TOML theme parsing, best-effort diagnostics, path resolution, and asset discovery.
- `FontCache`: font registration, fallback selection, and safe face replacement.
- Icon handling: icon lookup, custom asset loading, and default/placeholder fallback.
- Panels/widgets/overlays: consume theme values, expose refresh hooks where needed, and request redraw; they do not own parsing or path resolution.
- `App`: applies theme updates, refreshes shared caches, and schedules repaint for every affected surface.

## Testing and validation

Tests cover:

- Parsing colors, opacity, dimensions, typography, and enum values.
- Invalid syntax/types and unknown keys while retaining valid neighboring updates.
- Absolute, theme-relative, XDG, and system asset path precedence.
- Missing/malformed font and icon fallback.
- Change classification for token-only, font, and icon updates.
- Refresh boundaries that preserve valid resource ownership and avoid dangling faces/assets.

A source audit verifies that visual literals are either intentional protocol/layout constants or named theme tokens. Meson build verification and manual runtime checks cover the panel, launcher, lock, power, notification, OSD, tray, and tooltip surfaces. Default Nocturne rendering must remain visually equivalent before and after the cohesion pass.

## Non-goals

- No animation framework or animation timing behavior.
- No damage-aware rendering optimization.
- No new configuration path or reload trigger implementation.
- No redesign of the Nocturne palette, widget layout, or interaction model.
- No requirement for arbitrary third-party icon-theme semantics beyond the supported lookup/fallback interface.
