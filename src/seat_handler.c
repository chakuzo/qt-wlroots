/*
 * seat_handler.c - Seat management with keyboard, pointer, and input handling
 * 
 * CRITICAL: A functioning seat with keyboard capability is REQUIRED
 * for Wayland applications to work. Without keyboard focus, apps
 * will not receive any input events.
 */
#define _POSIX_C_SOURCE 200809L

/* MUST include generated protocol header BEFORE wlr/types/wlr_xdg_shell.h */
#include "xdg-shell-protocol.h"

#include "seat_handler.h"
#include "compositor_core.h"
#include "xdg_shell_handler.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* External accessors */
extern struct wl_display* comp_server_get_display(struct comp_server* server);
extern struct wlr_scene* comp_server_get_scene(struct comp_server* server);
extern struct wl_list* comp_server_get_views(struct comp_server* server);

/* Get current timestamp in milliseconds */
static uint32_t get_time_msec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Handle cursor image request */
static void handle_request_cursor(struct wl_listener* listener, void* data) {
    struct comp_seat* seat = wl_container_of(listener, seat, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event* event = data;
    
    /* In nested mode, we don't need to handle cursor images ourselves -
     * the parent compositor does it. This is just for acknowledgment. */
    (void)seat;
    (void)event;
    wlr_log(WLR_DEBUG, "Cursor request received");
}

/* Handle selection request */
static void handle_request_set_selection(struct wl_listener* listener, void* data) {
    struct comp_seat* seat = wl_container_of(listener, seat, request_set_selection);
    struct wlr_seat_request_set_selection_event* event = data;
    
    wlr_seat_set_selection(seat->seat, event->source, event->serial);
}

/* Handle new input device */
static void handle_new_input(struct wl_listener* listener, void* data) {
    struct comp_seat* seat = wl_container_of(listener, seat, new_input);
    struct wlr_input_device* device = data;
    
    wlr_log(WLR_INFO, "New input device: %s (type %d)", 
            device->name, device->type);
    
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct wlr_keyboard* keyboard = wlr_keyboard_from_input_device(device);
        
        /* Set keymap */
        wlr_keyboard_set_keymap(keyboard, seat->xkb_keymap);
        wlr_keyboard_set_repeat_info(keyboard, 25, 600);
        
        /* Set seat keyboard */
        wlr_seat_set_keyboard(seat->seat, keyboard);
        
        /* Update capabilities */
        uint32_t caps = WL_SEAT_CAPABILITY_KEYBOARD;
        if (seat->seat->pointer_state.focused_surface) {
            caps |= WL_SEAT_CAPABILITY_POINTER;
        }
        wlr_seat_set_capabilities(seat->seat, caps | WL_SEAT_CAPABILITY_POINTER);
        break;
    }
    case WLR_INPUT_DEVICE_POINTER:
        /* Pointer input - update capabilities */
        wlr_seat_set_capabilities(seat->seat, 
            seat->seat->capabilities | WL_SEAT_CAPABILITY_POINTER);
        break;
    default:
        break;
    }
}

/* Initialize seat */
bool comp_seat_init(struct comp_seat* seat, struct comp_server* server) {
    memset(seat, 0, sizeof(*seat));
    seat->server = server;
    
    struct wl_display* display = comp_server_get_display(server);
    
    /* Create seat */
    seat->seat = wlr_seat_create(display, "seat0");
    if (!seat->seat) {
        wlr_log(WLR_ERROR, "Failed to create seat");
        return false;
    }
    
    /* Initialize XKB for keyboard handling */
    seat->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!seat->xkb_context) {
        wlr_log(WLR_ERROR, "Failed to create xkb context");
        return false;
    }
    
    /* Create default keymap (us layout) */
    struct xkb_rule_names rules = {
        .rules = NULL,
        .model = NULL,
        .layout = "us",
        .variant = NULL,
        .options = NULL,
    };
    
    seat->xkb_keymap = xkb_keymap_new_from_names(seat->xkb_context, &rules,
                                                  XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!seat->xkb_keymap) {
        wlr_log(WLR_ERROR, "Failed to create xkb keymap");
        return false;
    }
    
    seat->xkb_state = xkb_state_new(seat->xkb_keymap);
    if (!seat->xkb_state) {
        wlr_log(WLR_ERROR, "Failed to create xkb state");
        return false;
    }
    
    /* wlroots 0.19: No virtual keyboard needed, seat handles it internally.
     * We just need to set the keymap on any keyboard that connects. */
    
    /* Set initial capabilities - CRITICAL! */
    wlr_seat_set_capabilities(seat->seat, 
        WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
    
    /* Setup listeners */
    seat->request_cursor.notify = handle_request_cursor;
    wl_signal_add(&seat->seat->events.request_set_cursor, &seat->request_cursor);
    
    seat->request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&seat->seat->events.request_set_selection, &seat->request_set_selection);
    
    seat->new_input.notify = handle_new_input;
    /* Note: new_input comes from backend, which we'll connect later */
    
    seat->initialized = true;
    wlr_log(WLR_INFO, "Seat initialized with keyboard and pointer capabilities");
    return true;
}

/* Cleanup seat */
void comp_seat_finish(struct comp_seat* seat) {
    if (!seat || !seat->initialized) return;
    
    wl_list_remove(&seat->request_cursor.link);
    wl_list_remove(&seat->request_set_selection.link);
    
    if (seat->xkb_state) xkb_state_unref(seat->xkb_state);
    if (seat->xkb_keymap) xkb_keymap_unref(seat->xkb_keymap);
    if (seat->xkb_context) xkb_context_unref(seat->xkb_context);
    
    seat->initialized = false;
}

/* Focus view - sends keyboard enter event */
void comp_seat_focus_view(struct comp_seat* seat, struct comp_view* view) {
    if (!seat || !seat->seat) return;
    
    if (!view || !view->mapped || !view->xdg_toplevel) {
        /* Clear focus */
        wlr_seat_keyboard_notify_clear_focus(seat->seat);
        return;
    }
    
    struct wlr_surface* surface = view->xdg_toplevel->base->surface;
    
    /* Get keyboard for modifiers */
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat->seat);
    
    /* Send keyboard enter - CRITICAL for apps to receive input! */
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat->seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    } else {
        /* No keyboard yet, still send enter with empty state */
        wlr_seat_keyboard_notify_enter(seat->seat, surface, NULL, 0, NULL);
    }
    
    wlr_log(WLR_DEBUG, "Keyboard focus sent to surface");
}

/* Get focused view */
struct comp_view* comp_seat_get_focused_view(struct comp_seat* seat) {
    if (!seat || !seat->seat) return NULL;
    
    struct wlr_surface* focused = seat->seat->keyboard_state.focused_surface;
    if (!focused) return NULL;
    
    /* Find view by surface */
    struct wl_list* views = comp_server_get_views(seat->server);
    struct comp_view* view;
    wl_list_for_each(view, views, link) {
        if (view->xdg_toplevel && 
            view->xdg_toplevel->base->surface == focused) {
            return view;
        }
    }
    
    return NULL;
}

/* Send key event */
void comp_seat_send_key(struct comp_seat* seat, uint32_t key, bool pressed) {
    if (!seat || !seat->seat) return;
    
    /* Update XKB state */
    xkb_state_update_key(seat->xkb_state, key + 8, 
        pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    
    /* Send to focused client */
    wlr_seat_keyboard_notify_key(seat->seat, get_time_msec(), key,
        pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
}

/* Send modifier state */
void comp_seat_send_modifiers(struct comp_seat* seat, uint32_t depressed,
                               uint32_t latched, uint32_t locked, uint32_t group) {
    if (!seat || !seat->seat) return;
    
    struct wlr_keyboard_modifiers mods = {
        .depressed = depressed,
        .latched = latched,
        .locked = locked,
        .group = group,
    };
    
    wlr_seat_keyboard_notify_modifiers(seat->seat, &mods);
}

/* Send pointer motion */
void comp_seat_send_pointer_motion(struct comp_seat* seat, double x, double y) {
    if (!seat || !seat->seat) return;
    
    seat->cursor_x = x;
    seat->cursor_y = y;
    
    /* Find surface under cursor */
    double sx, sy;
    struct comp_view* view = comp_seat_view_at(seat, x, y, &sx, &sy);
    
    if (view && view->mapped && view->xdg_toplevel) {
        struct wlr_surface* surface = view->xdg_toplevel->base->surface;
        
        /* Send enter if needed */
        if (surface != seat->seat->pointer_state.focused_surface) {
            wlr_seat_pointer_notify_enter(seat->seat, surface, sx, sy);
        }
        
        /* Send motion */
        wlr_seat_pointer_notify_motion(seat->seat, get_time_msec(), sx, sy);
    } else {
        /* Clear focus */
        wlr_seat_pointer_notify_clear_focus(seat->seat);
    }
}

/* Send pointer button */
void comp_seat_send_pointer_button(struct comp_seat* seat, uint32_t button, bool pressed) {
    if (!seat || !seat->seat) return;
    
    wlr_seat_pointer_notify_button(seat->seat, get_time_msec(), button,
        pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    
    /* Focus on click */
    if (pressed) {
        double sx, sy;
        struct comp_view* view = comp_seat_view_at(seat, seat->cursor_x, seat->cursor_y, &sx, &sy);
        if (view) {
            comp_view_focus(view);
        }
    }
}

/* Send pointer axis (scroll) */
void comp_seat_send_pointer_axis(struct comp_seat* seat, bool horizontal, double value) {
    if (!seat || !seat->seat) return;
    
    wlr_seat_pointer_notify_axis(seat->seat, get_time_msec(),
        horizontal ? WL_POINTER_AXIS_HORIZONTAL_SCROLL : WL_POINTER_AXIS_VERTICAL_SCROLL,
        value, (int32_t)value, WL_POINTER_AXIS_SOURCE_WHEEL,
        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
}

/* Find view at coordinates */
struct comp_view* comp_seat_view_at(struct comp_seat* seat, double x, double y,
                                     double* sx, double* sy) {
    if (!seat) return NULL;
    
    struct wlr_scene* scene = comp_server_get_scene(seat->server);
    if (!scene) return NULL;
    
    /* Scene graph hit test */
    struct wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, x, y, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }
    
    /* Walk up to find scene_tree with view data */
    struct wlr_scene_tree* tree = node->parent;
    while (tree) {
        if (tree->node.data) {
            return (struct comp_view*)tree->node.data;
        }
        tree = tree->node.parent;
    }
    
    return NULL;
}
