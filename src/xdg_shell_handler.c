/*
 * xdg_shell_handler.c - XDG shell protocol implementation
 * 
 * CRITICAL POINTS:
 * 1. configure events MUST be sent for surfaces to map
 * 2. Scene tree created at MAP time (not before - role may be 0)
 * 3. Listener cleanup uses flag to prevent double-removal
 */
#define _POSIX_C_SOURCE 200809L

/* MUST include generated protocol header BEFORE wlr/types/wlr_xdg_shell.h */
#include "xdg-shell-protocol.h"

#include "xdg_shell_handler.h"
#include "compositor_core.h"
#include "seat_handler.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

/* External accessors from compositor_core.c */
extern struct wl_display* comp_server_get_display(struct comp_server* server);
extern struct wlr_scene* comp_server_get_scene(struct comp_server* server);
extern struct comp_seat* comp_server_get_seat(struct comp_server* server);
extern struct wl_list* comp_server_get_views(struct comp_server* server);
extern void comp_server_notify_view_added(struct comp_server* server, struct comp_view* view);
extern void comp_server_notify_view_removed(struct comp_server* server, struct comp_view* view);

/* Forward declarations */
static void handle_xdg_toplevel_map(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_unmap(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_commit(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_destroy(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_request_move(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_request_resize(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_request_maximize(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_request_fullscreen(struct wl_listener* listener, void* data);
static void handle_xdg_toplevel_set_title(struct wl_listener* listener, void* data);

/* Remove all listeners for a view - safe with flag */
static void view_remove_listeners(struct comp_view* view) {
    if (!view->listeners_active) return;
    
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->set_title.link);
    
    view->listeners_active = false;
}

/* Handle new XDG toplevel */
static void handle_new_xdg_toplevel(struct wl_listener* listener, void* data) {
    struct comp_xdg_shell* shell = wl_container_of(listener, shell, new_xdg_toplevel);
    struct wlr_xdg_toplevel* toplevel = data;
    
    wlr_log(WLR_DEBUG, "New XDG toplevel: %s", 
            toplevel->title ? toplevel->title : "(untitled)");
    
    /* Allocate view */
    struct comp_view* view = calloc(1, sizeof(struct comp_view));
    if (!view) {
        wlr_log(WLR_ERROR, "Failed to allocate view");
        return;
    }
    
    view->server = shell->server;
    view->xdg_toplevel = toplevel;
    view->mapped = false;
    view->scene_tree = NULL;  /* Created at map time! */
    view->x = 50;  /* Default position */
    view->y = 50;
    
    /* Setup listeners */
    view->map.notify = handle_xdg_toplevel_map;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);
    
    view->unmap.notify = handle_xdg_toplevel_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
    
    view->commit.notify = handle_xdg_toplevel_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
    
    view->destroy.notify = handle_xdg_toplevel_destroy;
    wl_signal_add(&toplevel->events.destroy, &view->destroy);
    
    view->request_move.notify = handle_xdg_toplevel_request_move;
    wl_signal_add(&toplevel->events.request_move, &view->request_move);
    
    view->request_resize.notify = handle_xdg_toplevel_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);
    
    view->request_maximize.notify = handle_xdg_toplevel_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
    
    view->request_fullscreen.notify = handle_xdg_toplevel_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
    
    view->set_title.notify = handle_xdg_toplevel_set_title;
    wl_signal_add(&toplevel->events.set_title, &view->set_title);
    
    view->listeners_active = true;
    
    /* Add to server view list */
    wl_list_insert(comp_server_get_views(shell->server), &view->link);
    
    /* wlroots 0.19: Do NOT call wlr_xdg_surface_schedule_configure here!
     * The surface is not yet initialized. Configure will be sent automatically
     * after the client's first commit, or we send it in the commit handler
     * after initial_commit is set. */
    
    wlr_log(WLR_DEBUG, "XDG toplevel setup complete, waiting for initial commit");
}

/* Handle toplevel map - CRITICAL: create scene tree HERE */
static void handle_xdg_toplevel_map(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, map);
    (void)data;
    
    wlr_log(WLR_INFO, "XDG toplevel mapped: %s",
            view->xdg_toplevel->title ? view->xdg_toplevel->title : "(untitled)");
    
    /* NOW create scene tree - surface has valid role */
    struct wlr_scene* scene = comp_server_get_scene(view->server);
    view->scene_tree = wlr_scene_xdg_surface_create(&scene->tree, 
                                                     view->xdg_toplevel->base);
    if (!view->scene_tree) {
        wlr_log(WLR_ERROR, "Failed to create scene tree for view");
        return;
    }
    
    /* Store view pointer in scene node data */
    view->scene_tree->node.data = view;
    
    /* Set position */
    wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    
    view->mapped = true;
    
    /* Focus the new view */
    comp_view_focus(view);
    
    /* Notify Qt */
    comp_server_notify_view_added(view->server, view);
}

/* Handle toplevel unmap */
static void handle_xdg_toplevel_unmap(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, unmap);
    (void)data;
    
    wlr_log(WLR_INFO, "XDG toplevel unmapped");
    
    view->mapped = false;
    
    /* Notify Qt before cleanup */
    comp_server_notify_view_removed(view->server, view);
    
    /* Reset keyboard focus if this view had it */
    struct comp_seat* seat = comp_server_get_seat(view->server);
    if (seat && seat->seat) {
        struct wlr_surface* focused = seat->seat->keyboard_state.focused_surface;
        if (focused && focused == view->xdg_toplevel->base->surface) {
            wlr_seat_keyboard_notify_clear_focus(seat->seat);
        }
    }
}

/* Handle surface commit */
static void handle_xdg_toplevel_commit(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, commit);
    (void)data;
    
    /* wlroots 0.19: After initial_commit, the surface is initialized
     * and we can send configure events */
    if (view->xdg_toplevel->base->initial_commit) {
        if (!view->pending_configure) {
            /* Send initial configure - use fullscreen to avoid ALL decorations */
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, 640, 480);
            wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
            wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
            view->pending_configure = true;
            wlr_log(WLR_DEBUG, "Sent initial configure (640x480 fullscreen) after initial_commit");
        }
    }
    
    /* Notify that a frame was committed - trigger render */
    if (view->mapped) {
        comp_server_notify_frame_commit(view->server);
    }
}

/* Handle toplevel destroy */
static void handle_xdg_toplevel_destroy(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, destroy);
    (void)data;
    
    wlr_log(WLR_DEBUG, "XDG toplevel destroyed");
    
    /* Remove from server list */
    wl_list_remove(&view->link);
    
    /* Remove listeners safely */
    view_remove_listeners(view);
    
    /* Scene tree is automatically destroyed with surface */
    
    free(view);
}

/* Handle move request */
static void handle_xdg_toplevel_request_move(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, request_move);
    (void)data;
    wlr_log(WLR_DEBUG, "Move requested for: %s", 
            view->xdg_toplevel->title ? view->xdg_toplevel->title : "(untitled)");
    /* TODO: implement interactive move */
}

/* Handle resize request */
static void handle_xdg_toplevel_request_resize(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, request_resize);
    (void)data;
    wlr_log(WLR_DEBUG, "Resize requested");
    /* TODO: implement interactive resize */
}

/* Handle maximize request */
static void handle_xdg_toplevel_request_maximize(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, request_maximize);
    (void)data;
    wlr_log(WLR_DEBUG, "Maximize requested");
    /* Acknowledge but don't change size for now */
    wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

/* Handle fullscreen request */
static void handle_xdg_toplevel_request_fullscreen(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, request_fullscreen);
    (void)data;
    wlr_log(WLR_DEBUG, "Fullscreen requested");
    /* Acknowledge */
    wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

/* Handle title change */
static void handle_xdg_toplevel_set_title(struct wl_listener* listener, void* data) {
    struct comp_view* view = wl_container_of(listener, view, set_title);
    (void)data;
    wlr_log(WLR_DEBUG, "Title changed to: %s",
            view->xdg_toplevel->title ? view->xdg_toplevel->title : "(null)");
}

/* Handle new popup */
static void handle_new_xdg_popup(struct wl_listener* listener, void* data) {
    struct comp_xdg_shell* shell = wl_container_of(listener, shell, new_xdg_popup);
    struct wlr_xdg_popup* popup = data;
    (void)shell;
    
    wlr_log(WLR_DEBUG, "New XDG popup");
    
    /* wlroots 0.19: popup->parent is wlr_surface*, get xdg_surface from it */
    struct wlr_surface* parent_surface = popup->parent;
    if (!parent_surface) {
        wlr_log(WLR_ERROR, "Popup has no parent surface");
        return;
    }
    
    /* Get the xdg_surface from the parent wlr_surface */
    struct wlr_xdg_surface* parent_xdg = wlr_xdg_surface_try_from_wlr_surface(parent_surface);
    if (!parent_xdg) {
        wlr_log(WLR_ERROR, "Parent surface is not an xdg_surface");
        return;
    }
    
    /* Create popup scene tree - wlroots handles positioning */
    struct wlr_scene_tree* parent_tree = parent_xdg->data;
    if (parent_tree) {
        wlr_scene_xdg_surface_create(parent_tree, popup->base);
    }
}

/* Handle new decoration request */
static void handle_new_decoration(struct wl_listener* listener, void* data) {
    struct comp_xdg_shell* shell = wl_container_of(listener, shell, new_decoration);
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;
    
    wlr_log(WLR_DEBUG, "New decoration request, setting server-side");
    
    /* Tell the client that the server will handle decorations (i.e., none) */
    wlr_xdg_toplevel_decoration_v1_set_mode(decoration, 
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

/* Initialize XDG shell */
bool comp_xdg_shell_init(struct comp_xdg_shell* shell, struct comp_server* server) {
    shell->server = server;
    
    struct wl_display* display = comp_server_get_display(server);
    
    /* Create XDG shell - version 6 (latest stable features) */
    shell->xdg_shell = wlr_xdg_shell_create(display, 6);
    if (!shell->xdg_shell) {
        wlr_log(WLR_ERROR, "Failed to create xdg_shell");
        return false;
    }
    
    /* Create decoration manager - tells clients not to draw decorations */
    shell->decoration_manager = wlr_xdg_decoration_manager_v1_create(display);
    if (shell->decoration_manager) {
        shell->new_decoration.notify = handle_new_decoration;
        wl_signal_add(&shell->decoration_manager->events.new_toplevel_decoration,
                      &shell->new_decoration);
        wlr_log(WLR_INFO, "XDG decoration manager created");
    }
    
    /* Listen for new toplevels */
    shell->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
    wl_signal_add(&shell->xdg_shell->events.new_toplevel, &shell->new_xdg_toplevel);
    
    /* Listen for new popups */
    shell->new_xdg_popup.notify = handle_new_xdg_popup;
    wl_signal_add(&shell->xdg_shell->events.new_popup, &shell->new_xdg_popup);
    
    wlr_log(WLR_INFO, "XDG shell initialized");
    return true;
}

/* Cleanup XDG shell */
void comp_xdg_shell_finish(struct comp_xdg_shell* shell) {
    if (!shell) return;
    wl_list_remove(&shell->new_xdg_toplevel.link);
    wl_list_remove(&shell->new_xdg_popup.link);
    if (shell->decoration_manager) {
        wl_list_remove(&shell->new_decoration.link);
    }
}

/* Focus a view - CRITICAL for keyboard input */
void comp_view_focus(struct comp_view* view) {
    if (!view || !view->mapped || !view->xdg_toplevel) return;
    
    struct comp_server* server = view->server;
    struct comp_seat* seat = comp_server_get_seat(server);
    if (!seat || !seat->seat) return;
    
    struct wlr_surface* prev_surface = seat->seat->keyboard_state.focused_surface;
    struct wlr_surface* new_surface = view->xdg_toplevel->base->surface;
    
    if (prev_surface == new_surface) {
        return;  /* Already focused */
    }
    
    /* Deactivate previous */
    if (prev_surface) {
        struct wlr_xdg_toplevel* prev_toplevel = 
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }
    
    /* Activate new - raise to top */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    
    /* Move to front of view list */
    wl_list_remove(&view->link);
    wl_list_insert(comp_server_get_views(server), &view->link);
    
    /* Set activated state */
    wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
    
    /* Send keyboard focus - CRITICAL! */
    comp_seat_focus_view(seat, view);
}
