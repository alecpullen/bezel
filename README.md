# bezel

A single-process Wayland shell panel written in C++20. Designed for the **MangoWC** compositor.

## Features

- Per-output panels anchored to the bottom of each monitor
- Workspace switcher with tiling maps (via dwl-ipc)
- Window list with app icons and active-window highlight
- Battery status, clock
- On-demand launcher
- Session lock (ext-session-lock hard lock with PAM auth, layer-shell soft-lock fallback)
- Idle detection + idle-triggered lock + idle inhibitor
- Power menu (logind: suspend / hibernate / reboot / shut down / log out)
- Unix domain socket for external control (`$XDG_RUNTIME_DIR/bezel.sock`)
- Nocturne theme (Tokyo Night-derived design tokens)

## Requirements

- Wayland compositor with `zwlr_layer_shell_v1` — tested against **MangoWC**
- `wayland-client`, `wayland-egl`, `EGL`, `GLESv2`
- `sdbus-c++` (D-Bus / UPower)
- `xkbcommon` (keyboard input / keysym mapping)
- `libpam` (`libpam0g-dev` / `libpam-dev`) — PAM authentication for the lock screen
- `freetype2`, `harfbuzz`
- `librsvg-2.0` (optional, for SVG icons)
- `wayland-protocols` ≥ 1.32 for the `ext-session-lock-v1` and `ext-idle-notify-v1` protocols
- `meson` + `ninja`

## Build

```bash
meson setup buildDir
meson compile -C buildDir
```

Run against a Wayland session:

```bash
./buildDir/bezel
```

> GNOME and KDE sessions will fail at init — `zwlr_layer_shell_v1` is not available there.

## Control socket

Send commands to a running instance:

```bash
echo "toggle_launcher" | nc -U $XDG_RUNTIME_DIR/bezel.sock
echo "lock"               | nc -U $XDG_RUNTIME_DIR/bezel.sock   # lock the session
echo "power"              | nc -U $XDG_RUNTIME_DIR/bezel.sock   # open the power menu
echo "inhibit 1"          | nc -U $XDG_RUNTIME_DIR/bezel.sock   # inhibit idle (0 to re-enable, bare "inhibit" toggles)
```

While the session is soft-locked, all socket commands except `unlock` are ignored.

## Lock screen

The lock screen authenticates via PAM using the service name `bezel`. The
default user is the invoking user (override with `lock_user` in config). To
authenticate on the hard lock path, the compositor must advertise
`ext-session-lock-v1`; otherwise bezel falls back to a best-effort
layer-shell soft lock. Install a PAM service file, e.g. `/etc/pam.d/bezel`:

```
@include common-auth
```

## Docs

- [`docs/BEZEL_DESIGN_SPEC.md`](docs/BEZEL_DESIGN_SPEC.md) — UI/UX design spec and token reference
- [`docs/milestone-tracker.md`](docs/milestone-tracker.md) — implementation roadmap
