# wlroots-qt-compositor

A Wayland compositor that embeds client windows directly into Qt/QML applications using wlroots as the backend.

![License](https://img.shields.io/badge/license-MIT-blue.svg)

## Overview

This project demonstrates how to create a Wayland compositor using wlroots and embed the client surfaces directly into a Qt6/QML interface. Unlike traditional compositors that render to their own window, this implementation uses a headless wlroots backend and renders client content into QML items.

### Features

- **Embedded Wayland Surfaces**: Display Wayland client windows as QML items
- **Full Input Support**: Keyboard and mouse input forwarding to clients
- **Dynamic Resizing**: Client windows automatically resize to match QML item dimensions
- **Multiple Views**: Support for multiple embedded client windows
- **Fullscreen Mode**: Clients render without decorations for seamless embedding

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Qt6/QML Application                   │
│  ┌─────────────────┐       ┌─────────────────┐          │
│  │  EmbeddedView 0 │       │  EmbeddedView 1 │          │
│  │   (QQuickItem)  │       │   (QQuickItem)  │          │
│  └────────┬────────┘       └────────┬────────┘          │
│           │                         │                    │
│           └──────────┬──────────────┘                    │
│                      │                                   │
│           ┌──────────▼──────────┐                        │
│           │  CompositorWrapper  │  (C++ Qt wrapper)      │
│           └──────────┬──────────┘                        │
└──────────────────────┼───────────────────────────────────┘
                       │
┌──────────────────────┼───────────────────────────────────┐
│                      │        wlroots (C)                │
│           ┌──────────▼──────────┐                        │
│           │   Headless Backend  │                        │
│           │   Pixman Renderer   │                        │
│           └──────────┬──────────┘                        │
│                      │                                   │
│    ┌─────────────────┼─────────────────┐                 │
│    │                 │                 │                 │
│    ▼                 ▼                 ▼                 │
│ XDG Shell         Seat           Scene Graph             │
│ (windows)      (input)          (rendering)              │
└──────────────────────────────────────────────────────────┘
                       │
                       ▼
              Wayland Clients
           (weston-terminal, etc.)
```

## Requirements

### Arch Linux

```bash
sudo pacman -S \
    wlroots \
    wayland \
    wayland-protocols \
    qt6-base \
    qt6-declarative \
    cmake \
    ninja \
    pkg-config \
    libxkbcommon \
    pixman
```

### Other Distributions

- wlroots >= 0.18 (tested with 0.19)
- Qt6 (Core, Gui, Quick, Widgets)
- wayland-server
- wayland-protocols
- libxkbcommon
- pixman
- CMake >= 3.16

## Building

```bash
# Clone the repository
git clone https://github.com/user/wlroots-qt-compositor.git
cd wlroots-qt-compositor

# Build
mkdir build && cd build
cmake .. -G Ninja
ninja

# Or use the convenience script (Arch Linux)
./build-arch.sh
```

## Running

The compositor must run inside an existing Wayland or X11 session:

```bash
# Run with software rendering (default)
./build/wlroots-qt-compositor

# Run with hardware acceleration (if available)
./build/wlroots-qt-compositor --hardware

# Or via environment variable
WLROOTS_QT_HARDWARE=1 ./build/wlroots-qt-compositor

# In another terminal, launch a Wayland client
WAYLAND_DISPLAY=wayland-1 weston-terminal
```

Or use the convenience script:

```bash
./run-nested.sh
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `--hardware`, `-hw` | Use GPU-accelerated rendering (GLES2 + DMA-BUF) |
| `--software`, `-sw` | Use CPU-based rendering (Pixman) [default] |
| `--help`, `-h` | Show usage information |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `WLROOTS_QT_HARDWARE=1` | Enable hardware rendering |

## Project Structure

```
wlroots-qt-compositor/
├── include/
│   ├── compositor_core.h      # C API for wlroots compositor
│   ├── compositor_wrapper.h   # Qt/C++ wrapper class
│   ├── embedded_view.h        # QML item for displaying surfaces
│   ├── xdg_shell_handler.h    # XDG shell protocol handler
│   ├── seat_handler.h         # Input (keyboard/pointer) handling
│   └── output_handler.h       # Virtual output management
├── src/
│   ├── compositor_core.c      # Core compositor implementation
│   ├── compositor_wrapper.cpp # Qt integration layer
│   ├── embedded_view.cpp      # Surface rendering to QML
│   ├── xdg_shell_handler.c    # Window management
│   ├── seat_handler.c         # Input forwarding
│   ├── output_handler.c       # Headless output
│   └── main.cpp               # Application entry point
├── qml/
│   └── main.qml               # QML UI definition
├── CMakeLists.txt
└── README.md
```

## How It Works

1. **Headless Backend**: Unlike typical compositors, we use wlroots' headless backend which doesn't create its own window. This allows us to capture rendered frames.

2. **Pixman Renderer**: Software rendering via pixman produces CPU-accessible buffers that can be copied into Qt textures.

3. **Frame Capture**: When clients commit new content, we render the scene and copy the pixel data into QImage buffers.

4. **QML Integration**: The `EmbeddedView` QQuickItem displays these buffers as textures and forwards input events back to the compositor.

5. **Input Forwarding**: Mouse and keyboard events from Qt are translated to Wayland protocol events and sent to the focused client.

## Rendering Backends

### Software Rendering (Default)

Uses Pixman for CPU-based rendering:

```bash
./wlroots-qt-compositor --software
```

- Works on any system
- No GPU dependencies
- Higher CPU usage
- Good for debugging and embedded systems

### Hardware Rendering

Uses GLES2 with DMA-BUF for GPU-accelerated rendering:

```bash
./wlroots-qt-compositor --hardware
```

- Zero-copy buffer sharing (when DMA-BUF works)
- Lower CPU usage
- Requires working GPU drivers
- Falls back to software if unavailable

The hardware backend attempts to use DMA-BUF for zero-copy rendering. If DMA-BUF
is not available (e.g., in some nested Wayland scenarios), it falls back to
copying pixels but still uses GPU rendering.

## Configuration

### Changing the Default Window Size

Edit `src/xdg_shell_handler.c`:

```c
wlr_xdg_toplevel_set_size(view->xdg_toplevel, 800, 600);
```

### Modifying the QML Layout

Edit `qml/main.qml` to customize the UI layout, add more view slots, or change styling.

## Troubleshooting

### "Failed to create headless backend"

Ensure you're running inside a Wayland or X11 session with proper permissions.

### Client shows window decorations

Some clients ignore the fullscreen hint. Try `weston-terminal` which respects it, or configure your client to disable CSD (Client-Side Decorations).

### No keyboard input

1. Click on the EmbeddedView to focus it
2. Check logs for "Keyboard focus sent to surface"
3. Ensure the virtual keyboard was created successfully

### Black screen / no rendering

- Verify clients are connecting ("XDG toplevel mapped" in logs)
- Check that clients use SHM buffers (most do by default)

## License

MIT License

```
Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Dependencies

This project uses:
- [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) (MIT License)
- [Qt6](https://www.qt.io/) (LGPL v3 / Commercial)
- [Wayland](https://wayland.freedesktop.org/) (MIT License)
- [libxkbcommon](https://xkbcommon.org/) (MIT License)
- [pixman](http://pixman.org/) (MIT License)

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## Acknowledgments

- The wlroots team for the excellent compositor library
- The Qt Project for the UI framework
- The Wayland community for protocol specifications
