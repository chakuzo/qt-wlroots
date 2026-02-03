/*
 * seat_handler.h - Seat management (keyboard, pointer, touch)
 * 
 * CRITICAL: A seat with keyboard capability MUST exist for apps to work!
 */
#ifndef SEAT_HANDLER_H
#define SEAT_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wayland-server-core.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>

struct comp_server;
struct comp_view;

/* Seat state */
struct comp_seat {
    struct wlr_seat* seat;
    struct comp_server* server;
    
    /* XKB keyboard state */
    struct xkb_context* xkb_context;
    struct xkb_keymap* xkb_keymap;
    struct xkb_state* xkb_state;
    
    /* Cursor state */
    double cursor_x, cursor_y;
    
    /* Input device listeners */
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    
    bool initialized;
};

/* Initialize seat */
bool comp_seat_init(struct comp_seat* seat, struct comp_server* server);

/* Cleanup seat */
void comp_seat_finish(struct comp_seat* seat);

/* Focus management - MUST send keyboard enter/leave! */
void comp_seat_focus_view(struct comp_seat* seat, struct comp_view* view);
struct comp_view* comp_seat_get_focused_view(struct comp_seat* seat);

/* Keyboard input forwarding */
void comp_seat_send_key(struct comp_seat* seat, uint32_t key, bool pressed);
void comp_seat_send_modifiers(struct comp_seat* seat, uint32_t depressed, 
                               uint32_t latched, uint32_t locked, uint32_t group);

/* Pointer input forwarding */
void comp_seat_send_pointer_motion(struct comp_seat* seat, double x, double y);
void comp_seat_send_pointer_button(struct comp_seat* seat, uint32_t button, bool pressed);
void comp_seat_send_pointer_axis(struct comp_seat* seat, bool horizontal, double value);

/* Get view at coordinates */
struct comp_view* comp_seat_view_at(struct comp_seat* seat, double x, double y, 
                                     double* sx, double* sy);

#ifdef __cplusplus
}
#endif

#endif /* SEAT_HANDLER_H */
