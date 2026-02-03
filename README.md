# wlroots-qt-compositor

Ein funktionierender Wayland Compositor mit wlroots 0.19 (MIT) und Qt6 (LGPL) - **ohne QtWayland** (GPL).

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

## Kritische Implementation Details

### 1. Funktionsnamen-Prefix

Alle C-Wrapper-Funktionen verwenden `comp_*` statt `wlr_*` um Konflikte zu vermeiden:
```c
// RICHTIG
struct comp_server* comp_server_create(void);

// FALSCH - würde mit wlroots kollidieren
struct wlr_server* wlr_server_create(void);
```

### 2. XDG Surface Configure Events

**KRITISCH**: Ohne initiales Configure-Event mappen Apps nie!
```c
// In handle_new_xdg_toplevel():
wlr_xdg_toplevel_set_size(toplevel, 0, 0);
wlr_xdg_toplevel_set_activated(toplevel, false);
wlr_xdg_surface_schedule_configure(toplevel->base);
```

### 3. Scene Tree bei MAP erstellen

Scene Tree darf erst bei `map` Event erstellt werden (nicht vorher wegen role=0):
```c
// RICHTIG - in map handler
static void handle_xdg_toplevel_map(...) {
    view->scene_tree = wlr_scene_xdg_surface_create(&scene->tree, 
                                                     view->xdg_toplevel->base);
}

// FALSCH - in new_toplevel handler (role noch nicht gesetzt)
static void handle_new_xdg_toplevel(...) {
    // view->scene_tree = ...  // CRASH!
}
```

### 4. Seat mit Keyboard Capability

**KRITISCH**: Ohne Seat empfangen Apps keine Tastatureingaben!
```c
wlr_seat_set_capabilities(seat->seat, 
    WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
```

### 5. Listener Cleanup mit Flag

Verhindert Double-Free bei Cleanup:
```c
struct comp_view {
    // ...
    bool listeners_active;
};

static void view_remove_listeners(struct comp_view* view) {
    if (!view->listeners_active) return;
    wl_list_remove(&view->map.link);
    // ...
    view->listeners_active = false;
}
```

### 6. Output-Erstellung nach Backend-Start

Für nested Backends (Wayland/X11) werden Outputs erst nach `wlr_backend_start()` erstellt:
```c
// Backend sendet new_output Signal
mgr->new_output.notify = handle_new_output;
wl_signal_add(&backend->events.new_output, &mgr->new_output);

// Dann starten
wlr_backend_start(server->backend);  // -> löst new_output aus
```

### 7. Protokoll-Header Generierung

Arch liefert keine vorgenerierten Headers, CMake generiert sie:
```cmake
add_custom_command(
    OUTPUT ${SERVER_HEADER}
    COMMAND ${WAYLAND_SCANNER} server-header ${PROTOCOL_FILE} ${SERVER_HEADER}
    DEPENDS ${PROTOCOL_FILE}
)
```

### 8. C-Dateien in CMake

Explizit als C markieren für korrekte Kompilierung:
```cmake
set_source_files_properties(${C_SOURCES} PROPERTIES LANGUAGE C)
```

## Lizenz

- C Wrapper & Qt Integration: MIT
- wlroots: MIT
- Qt6: LGPL
- **Kein QtWayland** (GPL) - bewusst vermieden

## Troubleshooting

### "Failed to create wlr_backend"
- Läuft in graphischer Umgebung? (`echo $WAYLAND_DISPLAY` oder `echo $DISPLAY`)
- Backend erzwingen: `WLR_BACKENDS=x11 ./run-nested.sh`

### Apps mappen nicht / zeigen nichts an
- Configure Event vergessen? Check `wlr_xdg_surface_schedule_configure()`
- Seat hat Keyboard Capability? Check `wlr_seat_set_capabilities()`

### Segfault bei View Creation
- Scene Tree bei `map` erstellen, nicht bei `new_toplevel`!

### Keine Tastatureingabe
- Keyboard Focus senden: `wlr_seat_keyboard_notify_enter()`
- XKB Keymap initialisiert?
