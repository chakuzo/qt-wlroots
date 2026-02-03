/*
 * xdg_shell_handler.h - XDG shell protocol handler
 */
#ifndef XDG_SHELL_HANDLER_H
#define XDG_SHELL_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wayland-server-core.h>

/* Forward declarations to avoid including wlr_xdg_shell.h in header */
struct wlr_xdg_shell;
struct wlr_xdg_toplevel;
struct wlr_scene_tree;

struct comp_server;
struct comp_view;

/* XDG shell state */
struct comp_xdg_shell {
    struct wlr_xdg_shell* xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct comp_server* server;
};

/* View representing an XDG toplevel */
struct comp_view {
    struct wl_list link;  /* comp_server.views */
    struct comp_server* server;
    struct wlr_xdg_toplevel* xdg_toplevel;
    struct wlr_scene_tree* scene_tree;
    
    /* Position */
    int32_t x, y;
    
    /* State tracking */
    bool mapped;
    bool pending_configure;
    uint32_t pending_serial;
    
    /* Listeners - with cleanup flags */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener set_title;
    
    bool listeners_active;
};

/* Initialize XDG shell */
bool comp_xdg_shell_init(struct comp_xdg_shell* shell, struct comp_server* server);

/* Cleanup XDG shell */
void comp_xdg_shell_finish(struct comp_xdg_shell* shell);

/* View operations */
void comp_view_focus(struct comp_view* view);
void comp_view_begin_interactive(struct comp_view* view, int mode);

#ifdef __cplusplus
}
#endif

#endif /* XDG_SHELL_HANDLER_H */
