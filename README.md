# wlroots-qt-compositor

Ein funktionierender Wayland Compositor mit wlroots 0.19 (MIT) und Qt6 (LGPL).

## Features

- **wlroots 0.19** für vollständige Wayland-Funktionalität
- **Qt6/QML** UI ohne GPL-kontaminiertes QtWayland
- **Nested Mode** für einfache Entwicklung
- **XDG Shell** Support für Standard-Wayland-Apps
- **Keyboard & Pointer** Input Forwarding

## Architektur

```
┌─────────────────────────────────────────────────────────────┐
│                    Qt6 Application (LGPL)                   │
│  ┌─────────────────┐  ┌──────────────────────────────────┐ │
│  │  Control Panel  │  │      CompositorWrapper (C++)     │ │
│  │   (Qt Widgets)  │  │  - Event Loop Integration        │ │
│  └─────────────────┘  │  - Input Forwarding              │ │
│                       │  - View Management               │ │
│                       └──────────────────────────────────┘ │
└────────────────────────────────┬────────────────────────────┘
                                 │ C API (comp_*)
┌────────────────────────────────┴────────────────────────────┐
│                    C Wrapper Layer (MIT)                    │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────┐│
│  │ XDG Shell    │ │ Seat Handler │ │ Output Manager       ││
│  │ Handler      │ │ (Keyboard,   │ │ (Nested Backend)     ││
│  │ (Toplevels)  │ │  Pointer)    │ │                      ││
│  └──────────────┘ └──────────────┘ └──────────────────────┘│
└────────────────────────────────┬────────────────────────────┘
                                 │
┌────────────────────────────────┴────────────────────────────┐
│                    wlroots 0.19 (MIT)                       │
│  - Backend (Wayland/X11/DRM)                                │
│  - Renderer (Vulkan/GLES2/Pixman)                           │
│  - Scene Graph                                              │
│  - XDG Shell Protocol                                       │
└─────────────────────────────────────────────────────────────┘
```

## Arch Linux Installation

### Dependencies

```bash
# Core packages (Arch verwendet versionierte wlroots packages)
sudo pacman -S wlroots0.19 wayland wayland-protocols \
               qt6-base qt6-declarative \
               libxkbcommon pixman \
               cmake ninja pkgconf

# Optional: Test-Terminals
sudo pacman -S foot weston alacritty
```

### Build

```bash
./build-arch.sh
```

### Run

```bash
./run-nested.sh
```

Dann in einem anderen Terminal:
```bash
WAYLAND_DISPLAY=wayland-1 foot
# oder
WAYLAND_DISPLAY=wayland-1 weston-terminal
# oder  
WAYLAND_DISPLAY=wayland-1 alacritty
```



## Lizenz

- C Wrapper & Qt Integration: MIT
- wlroots: MIT
- Qt6: LGPL