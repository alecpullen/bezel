# bezel

A single-process Wayland shell panel written in C++20. Designed for the **MangoWC** compositor.

## Features

- Per-output panels anchored to the bottom of each monitor
- Workspace switcher with tiling maps (via dwl-ipc)
- Window list with app icons and active-window highlight
- Battery status, clock
- On-demand launcher (in progress)
- Unix domain socket for external control (`$XDG_RUNTIME_DIR/bezel.sock`)
- Nocturne theme (Tokyo Night-derived design tokens)

## Requirements

- Wayland compositor with `zwlr_layer_shell_v1` — tested against **MangoWC**
- `wayland-client`, `wayland-egl`, `EGL`, `GLESv2`
- `sdbus-c++` (D-Bus / UPower)
- `freetype2`, `harfbuzz`
- `librsvg-2.0` (optional, for SVG icons)
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
```

## Docs

- [`docs/BEZEL_DESIGN_SPEC.md`](docs/BEZEL_DESIGN_SPEC.md) — UI/UX design spec and token reference
- [`docs/milestone-tracker.md`](docs/milestone-tracker.md) — implementation roadmap
