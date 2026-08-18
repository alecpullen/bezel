# M10 Configuration and Live Reload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the M9 session-only parser with a central, typed TOML configuration model covering current M1–M9 behavior and apply safe changes through SIGHUP or the control socket without restarting bezel.

**Architecture:** `Config` owns typed sections and built-in defaults. `ConfigLoader` resolves the file, parses fields independently, emits diagnostics to stderr and a bounded state log, and returns a candidate configuration plus diagnostics. `ConfigDiff` classifies changes; `App` applies the diff in a fixed order, updating safe components in place and rebuilding only affected panels/components. Existing services/widgets never parse TOML.

**Tech Stack:** C++20, Meson/Ninja, toml++ (header-only), Wayland event loop, existing Unix control socket and service/widget classes.

## Global Constraints

- Preserve graceful degradation: missing or invalid configuration must never prevent bezel from starting.
- Configuration precedence is `$BEZEL_CONFIG`, `$XDG_CONFIG_HOME/bezel/config.toml`, `~/.config/bezel/config.toml`, then built-in defaults.
- Invalid syntax/types retain the previous value on reload and built-in defaults on startup; valid unrelated fields still apply.
- Unknown keys produce warnings and are ignored.
- SIGHUP handlers must only set a flag or wake the event loop; TOML parsing runs outside the signal handler.
- Do not add animation or damage-tracking work to this slice.
- Keep locks, notifications, OSDs, menus, and unaffected outputs alive whenever a reload can safely do so.

## File Map

- Create `src/config_types.hpp`: typed module, theme, layout, launcher, keybinding, session, and logging sections plus `Config` defaults.
- Replace `src/config.hpp` / `src/config.cpp`: `ConfigLoader`, path resolution, field-level parsing, diagnostics, and bounded file logging; retain `SessionConfig::systemUser()` compatibility or migrate callers in the same task.
- Create `src/config_diff.hpp` / `src/config_diff.cpp`: equality and change classification for in-place, rebuild, and deferred fields.
- Create `src/config_test.cpp`: focused loader/diff tests using temporary files and isolated environment variables.
- Modify `meson.build`: toml++ dependency, config/diff sources, and a test executable.
- Modify `src/main.cpp`: store `Config`, install SIGHUP handling, expose reload command, and coordinate diff application.
- Modify `src/control_socket.cpp` only if command normalization/dispatch needs a reload-safe addition.
- Modify `src/idle_service.hpp/.cpp`, `src/lock_service.hpp/.cpp`, launcher/service/widget headers and implementations only where narrow reconfigure methods are required.
- Modify `src/panel.hpp/.cpp` and `src/theme.hpp` to accept updated theme/layout snapshots and rebuild affected widget trees without changing unrelated behavior.
- Modify `README.md` and `docs/milestone-tracker.md` with the final schema, precedence, diagnostics, and reload commands.

### Task 1: Introduce typed configuration sections and TOML dependency

**Files:**
- Create: `src/config_types.hpp`
- Modify: `meson.build`
- Test: `src/config_test.cpp`

**Interfaces:**
- Produce `struct Config` with `ModulesConfig`, `ThemeConfig`, `LayoutConfig`, `LauncherConfig`, `KeybindingsConfig`, `SessionConfig`, and `LoggingConfig` members.
- Produce `Config::defaults()` returning a fully initialized built-in configuration.
- Preserve the existing session fields and `SessionConfig::systemUser()` contract.

- [ ] **Step 1: Add the dependency and test target skeleton.** Add `tomlplusplus` as a header-only dependency, add the new source files to the executable, and define a test executable linked to the config implementation and toml++.
- [ ] **Step 2: Write failing default tests.** Assert Nocturne/theme defaults, current M9 session defaults, all current modules enabled by default, bounded layout defaults, and that `Config::defaults()` is deterministic.
- [ ] **Step 3: Implement the structs and defaults.** Use explicit enums for panel anchor, workspace mode, window-label mode, and lock mode; use strings only for user commands/paths/key sequences. Keep module names matching the spec (`clock`, `battery`, `network`, `volume`, `mpris`, `tray`, `notifications`, `workspaces`, `windows`, `launcher`, `power`).
- [ ] **Step 4: Run the focused test.** Run `meson setup buildDir` or `meson setup --reconfigure buildDir`, then `meson test -C buildDir bezel-config`; expected result is PASS.
- [ ] **Step 5: Commit.** `git add meson.build src/config_types.hpp src/config_test.cpp && git commit -m "feat(config): add typed configuration model"`

### Task 2: Implement path resolution, field-level parsing, and diagnostics

**Files:**
- Modify: `src/config.hpp`, `src/config.cpp`
- Modify: `src/config_test.cpp`

**Interfaces:**
- Produce `struct ConfigDiagnostic { Severity severity; std::string path; std::string section; std::string key; std::string message; }`.
- Produce `struct ConfigLoadResult { Config config; std::vector<ConfigDiagnostic> diagnostics; bool fileFound; }`.
- Produce `ConfigLoader::resolvePath()` and `ConfigLoader::load(const Config& previous, bool startup)`.
- Produce `ConfigLoader::writeDiagnostics(...)` for stderr and `$XDG_STATE_HOME/bezel/config.log` (fallback `$HOME/.local/state/bezel/config.log`).

- [ ] **Step 1: Add failing tests for precedence and missing files.** Use a temporary directory and environment overrides to verify `$BEZEL_CONFIG` wins, then XDG config, then HOME fallback; missing files return defaults with no error diagnostic.
- [ ] **Step 2: Add failing tests for valid and invalid fields.** Cover booleans, bounded integers/floats, `#rrggbb` strings, enums, arrays of widget names, launcher paths/limits, keybinding strings, and session values. Assert one invalid value leaves only that field unchanged while valid fields from the same file apply.
- [ ] **Step 3: Add failing tests for syntax and unknown keys.** Assert malformed TOML yields diagnostics but still returns the previous/default config, and unknown keys produce warnings without changing known values.
- [ ] **Step 4: Implement loader and validators.** Parse each supported key independently with toml++ type checks; catch parser exceptions; validate ranges before assignment; report source path/section/key in every diagnostic. Do not silently reinterpret wrong TOML types. Preserve the previous snapshot for reload failures and defaults for startup failures.
- [ ] **Step 5: Implement bounded diagnostics logging.** Create the state directory with restrictive permissions, append timestamped diagnostics, and rotate before a configured byte limit by retaining one backup file. Logging failures must be reported to stderr but must not abort loading.
- [ ] **Step 6: Run focused tests.** `meson test -C buildDir bezel-config`; expected result is PASS for precedence, partial application, diagnostics, and logging fallback.
- [ ] **Step 7: Commit.** `git add src/config.hpp src/config.cpp src/config_test.cpp && git commit -m "feat(config): parse TOML with best-effort diagnostics"`

### Task 3: Add diff classification and config-specific tests

**Files:**
- Create: `src/config_diff.hpp`, `src/config_diff.cpp`
- Modify: `src/config_test.cpp`
- Modify: `meson.build`

**Interfaces:**
- Produce `enum class ConfigChangeClass { None, InPlace, Rebuild, Deferred }`.
- Produce `struct ConfigDiff` containing changed field groups and `ConfigChangeClass classification() const`.
- Produce `ConfigDiff diffConfig(const Config& oldConfig, const Config& newConfig)`.

- [ ] **Step 1: Write failing classification tests.** Verify logging, keybindings, launcher behavior, theme tokens, layout/widget order, module enablement, and session timeout changes classify as specified; unknown/unrepresented equality produces `None`.
- [ ] **Step 2: Implement explicit field comparisons.** Avoid pointer identity or whole-struct byte comparisons. Mark safe values `InPlace`, widget-tree/panel changes `Rebuild`, and Wayland-object recreation fields `Deferred`; preserve independent group flags so one deferred change does not block safe changes.
- [ ] **Step 3: Run tests.** `meson test -C buildDir bezel-config`; expected result is PASS.
- [ ] **Step 4: Commit.** `git add src/config_diff.hpp src/config_diff.cpp src/config_test.cpp meson.build && git commit -m "feat(config): classify reload changes"`

### Task 4: Wire reload triggers into the event loop

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/control_socket.cpp` only if needed
- Modify: `src/config.hpp`, `src/config.cpp`
- Modify: `src/config_test.cpp`

**Interfaces:**
- Add `volatile sig_atomic_t g_reloadRequested` (or an equivalent self-pipe/event-loop-safe flag) and a SIGHUP handler that only sets it.
- Add `App::requestConfigReload()` and `App::reloadConfig()`.
- Extend `handleSocketCommand` with exact command `reload`, returning/logging an acknowledgement through the existing socket protocol.

- [ ] **Step 1: Write failing unit tests for reload request state.** Assert repeated SIGHUP/request calls coalesce into one pending reload and socket whitespace normalization does not turn unrelated commands into reload.
- [ ] **Step 2: Install the SIGHUP handler after process initialization.** Keep the handler async-signal-safe and restore/avoid global teardown hazards.
- [ ] **Step 3: Poll the reload flag in the normal loop.** Invoke `ConfigLoader::load(config_, false)` once per pending request, write diagnostics, and retain `config_` on total failure while still applying valid fields.
- [ ] **Step 4: Implement the socket command.** Make `reload` schedule the same normal-loop path rather than parsing synchronously inside socket dispatch; refuse it while hard/soft locked according to the existing security policy, or document the safe exception if reload is needed for recovery.
- [ ] **Step 5: Run build and focused tests.** `meson compile -C buildDir` and `meson test -C buildDir bezel-config`; expected build/tests PASS.
- [ ] **Step 6: Commit.** `git add src/main.cpp src/config.hpp src/config.cpp src/control_socket.cpp src/config_test.cpp && git commit -m "feat(config): trigger reload via SIGHUP and socket"`

### Task 5: Apply in-place settings and rebuild affected panels/widgets

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/theme.hpp`, `src/panel.hpp`, `src/panel.cpp`
- Modify: relevant widget/service headers and implementations (`clock`, battery/network/audio/mpris/tray, launcher, power, idle/lock)
- Modify: `src/config_diff.*`
- Modify: `src/config_test.cpp`

**Interfaces:**
- Add narrow methods such as `Panel::applyTheme(const Theme&)`, `Panel::rebuildLayout(const LayoutConfig&, const ModulesConfig&)`, `IdleService::reconfigure(const SessionConfig&)`, and `LockService::reconfigure(const SessionConfig&)`; exact signatures must use const references and return `bool` when a component rejects a change.
- Add `App::applyConfigDiff(const Config&, const ConfigDiff&)`.

- [ ] **Step 1: Write failing tests for application ordering.** Use a small test double or extracted coordinator to assert logging → keybindings/launcher → theme/layout → modules/services → session order and that a rejected component does not prevent unrelated groups from applying.
- [ ] **Step 2: Add reconfigure methods to services.** Update idle/lock timeouts and module enablement without recreating live D-Bus/Wayland objects where safe; ensure active lock/authentication state is never reset by a reload.
- [ ] **Step 3: Add panel/theme update methods.** Rebuild only the affected widget list, preserve panel/output identity, request redraw, and ensure every new value comes from the typed theme/layout config rather than hardcoded replacements.
- [ ] **Step 4: Apply launcher/keybinding changes.** Update search limits/paths and replace key routing bindings in place; retain an open launcher/menu when its target remains valid, otherwise close only that transient surface.
- [ ] **Step 5: Implement the coordinator.** Apply independent diff groups in deterministic order, defer only unsafe Wayland recreation, and log deferred fields with their reason. Never destroy unrelated outputs or singleton services.
- [ ] **Step 6: Run tests and build.** `meson test -C buildDir bezel-config` followed by `meson compile -C buildDir`; expected result PASS.
- [ ] **Step 7: Commit.** `git add src && git commit -m "feat(config): apply live configuration changes"`

### Task 6: Document the schema and perform runtime validation

**Files:**
- Modify: `README.md`
- Modify: `docs/milestone-tracker.md`
- Modify: `src/config_test.cpp` if a discovered edge case needs regression coverage

**Interfaces:**
- Document the complete supported TOML schema, precedence, best-effort behavior, diagnostics path/rotation, and `reload` command.

- [ ] **Step 1: Add a concrete sample configuration.** Include modules, layout, theme/status colors, launcher, keybindings, logging, and session keys with valid values and comments explaining enum choices/ranges.
- [ ] **Step 2: Document operational behavior.** State that `kill -HUP <pid>` and `echo reload | nc -U "$XDG_RUNTIME_DIR/bezel.sock"` schedule reload; malformed fields are logged and retain prior values.
- [ ] **Step 3: Update milestone tracking.** Mark only the configuration/live-reload deliverables complete; do not mark animation or damage tracking complete.
- [ ] **Step 4: Run final verification.** Run `meson test -C buildDir`, `meson compile -C buildDir`, and manually verify missing config, malformed TOML, valid partial reload, SIGHUP, socket reload, and log-file fallback in a Linux Wayland session/container.
- [ ] **Step 5: Inspect diff and commit.** Run `git diff --check` and `git status --short`; commit with `git add README.md docs/milestone-tracker.md src/config_test.cpp && git commit -m "docs(config): document configuration and reload"`.

## Self-Review Checklist

- Spec coverage: Tasks 1–2 cover typed defaults, all schema sections, path precedence, best-effort parsing, diagnostics, unknown keys, validation, and rotation; Task 3 covers diff classification; Tasks 4–5 cover both reload triggers and hybrid application; Task 6 covers runtime validation and documentation.
- Scope: animation and damage-aware rendering are explicitly excluded.
- Placeholder scan: no step relies on an unspecified parser, path, command, or test name; all public interfaces used by later tasks are defined above.
- Type consistency: `ConfigLoader::load`, `ConfigLoadResult`, `ConfigDiff`, `diffConfig`, `App::reloadConfig`, and `App::applyConfigDiff` are the shared interfaces across tasks.
