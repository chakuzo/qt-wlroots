/*
 * compositor_core.c - Compositor with headless backend for Qt embedding
 * 
 * Uses headless backend + pixman renderer so we can read pixels back to Qt
 */
#define _POSIX_C_SOURCE 200809L

/* MUST include generated protocol header BEFORE wlr/types/wlr_xdg_shell.h */
#include "xdg-shell-protocol.h"

#include "compositor_core.h"
#include "xdg_shell_handler.h"
#include "seat_handler.h"
#include "output_handler.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>
#include <drm_fourcc.h>
#include <pixman.h>

/* Server structure - internal */
struct comp_server {
    /* Wayland core */
    struct wl_display* display;
    struct wl_event_loop* event_loop;
    const char* socket;
    
    /* wlroots backend - headless for embedded rendering */
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    
    /* Scene graph */
    struct wlr_scene* scene;
    struct wlr_scene_output_layout* scene_layout;
    
    /* Protocol implementations */
    struct wlr_compositor* compositor;
    struct wlr_subcompositor* subcompositor;
    struct wlr_data_device_manager* data_device_manager;
    
    /* Subsystems */
    struct comp_xdg_shell xdg_shell;
    struct comp_seat seat;
    struct comp_output_manager output_manager;
    
    /* View list */
    struct wl_list views;
    
    /* Callbacks for Qt integration */
    comp_frame_callback_t frame_callback;
    void* frame_callback_data;
    comp_view_callback_t view_callback;
    void* view_callback_data;
    
    /* State */
    bool running;
    bool backend_started;
};

/* Create server instance */
struct comp_server* comp_server_create(void) {
    wlr_log_init(WLR_DEBUG, NULL);
    
    struct comp_server* server = calloc(1, sizeof(struct comp_server));
    if (!server) {
        wlr_log(WLR_ERROR, "Failed to allocate server");
        return NULL;
    }
    
    wl_list_init(&server->views);
    
    /* Create wayland display */
    server->display = wl_display_create();
    if (!server->display) {
        wlr_log(WLR_ERROR, "Failed to create wl_display");
        free(server);
        return NULL;
    }
    
    server->event_loop = wl_display_get_event_loop(server->display);
    
    wlr_log(WLR_INFO, "Server created");
    return server;
}

/* Initialize backend - HEADLESS for embedding in Qt */
bool comp_server_init_backend(struct comp_server* server) {
    if (!server) return false;
    
    /* Create HEADLESS backend - no window, we render to buffers */
    server->backend = wlr_headless_backend_create(server->event_loop);
    if (!server->backend) {
        wlr_log(WLR_ERROR, "Failed to create headless backend");
        return false;
    }
    
    /* Create PIXMAN renderer for software rendering (CPU-readable) */
    server->renderer = wlr_pixman_renderer_create();
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "Failed to create pixman renderer");
        return false;
    }
    
    wlr_renderer_init_wl_display(server->renderer, server->display);
    
    /* Create allocator */
    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        wlr_log(WLR_ERROR, "Failed to create allocator");
        return false;
    }
    
    /* Create scene graph */
    server->scene = wlr_scene_create();
    if (!server->scene) {
        wlr_log(WLR_ERROR, "Failed to create scene");
        return false;
    }
    
    /* Initialize output manager first - needed for scene_layout */
    if (!comp_output_manager_init(&server->output_manager, server)) {
        wlr_log(WLR_ERROR, "Failed to init output manager");
        return false;
    }
    
    /* Connect output manager to backend for new_output events */
    extern void comp_output_manager_connect_backend(struct comp_output_manager* mgr,
                                                     struct wlr_backend* backend);
    comp_output_manager_connect_backend(&server->output_manager, server->backend);
    
    /* Attach scene to output layout */
    server->scene_layout = wlr_scene_attach_output_layout(server->scene, 
                                                           server->output_manager.layout);
    
    /* Create compositor interface */
    server->compositor = wlr_compositor_create(server->display, 6, server->renderer);
    if (!server->compositor) {
        wlr_log(WLR_ERROR, "Failed to create compositor");
        return false;
    }
    
    /* Create subcompositor */
    server->subcompositor = wlr_subcompositor_create(server->display);
    
    /* Create data device manager */
    server->data_device_manager = wlr_data_device_manager_create(server->display);
    
    /* Initialize XDG shell - CRITICAL for app windows */
    if (!comp_xdg_shell_init(&server->xdg_shell, server)) {
        wlr_log(WLR_ERROR, "Failed to init XDG shell");
        return false;
    }
    
    /* Initialize seat - CRITICAL for keyboard/pointer input */
    if (!comp_seat_init(&server->seat, server)) {
        wlr_log(WLR_ERROR, "Failed to init seat");
        return false;
    }
    
    wlr_log(WLR_INFO, "Headless backend initialized with pixman renderer");
    return true;
}

/* Start server */
bool comp_server_start(struct comp_server* server) {
    if (!server || !server->backend) return false;
    
    /* Add socket */
    server->socket = wl_display_add_socket_auto(server->display);
    if (!server->socket) {
        wlr_log(WLR_ERROR, "Failed to create socket");
        return false;
    }
    
    /* Start backend */
    if (!wlr_backend_start(server->backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        return false;
    }
    
    /* Create headless output (virtual display) */
    struct wlr_output* output = wlr_headless_add_output(server->backend, 1280, 720);
    if (!output) {
        wlr_log(WLR_ERROR, "Failed to create headless output");
        return false;
    }
    
    server->backend_started = true;
    server->running = true;
    
    wlr_log(WLR_INFO, "Server started on %s with headless output", server->socket);
    return true;
}

/* Destroy server */
void comp_server_destroy(struct comp_server* server) {
    if (!server) return;
    
    wlr_log(WLR_INFO, "Destroying server");
    
    /* Cleanup subsystems */
    comp_seat_finish(&server->seat);
    comp_xdg_shell_finish(&server->xdg_shell);
    comp_output_manager_finish(&server->output_manager);
    
    /* Destroy renderer */
    if (server->renderer) {
        wlr_renderer_destroy(server->renderer);
    }
    
    /* Destroy wayland display - cleans up everything else */
    if (server->display) {
        wl_display_destroy_clients(server->display);
        wl_display_destroy(server->display);
    }
    
    free(server);
}

/* Get socket name */
const char* comp_server_get_socket(struct comp_server* server) {
    return server ? server->socket : NULL;
}

/* Get event fd for polling */
int comp_server_get_event_fd(struct comp_server* server) {
    if (!server || !server->event_loop) return -1;
    return wl_event_loop_get_fd(server->event_loop);
}

/* Dispatch pending events */
void comp_server_dispatch_events(struct comp_server* server) {
    if (!server || !server->event_loop) return;
    wl_event_loop_dispatch(server->event_loop, 0);
    wl_display_flush_clients(server->display);
}

/* Flush clients */
void comp_server_flush_clients(struct comp_server* server) {
    if (!server || !server->display) return;
    wl_display_flush_clients(server->display);
}

/* Set frame callback */
void comp_server_set_frame_callback(struct comp_server* server,
                                     comp_frame_callback_t callback,
                                     void* user_data) {
    if (!server) return;
    server->frame_callback = callback;
    server->frame_callback_data = user_data;
}

/* Set view callback */
void comp_server_set_view_callback(struct comp_server* server,
                                    comp_view_callback_t callback,
                                    void* user_data) {
    if (!server) return;
    server->view_callback = callback;
    server->view_callback_data = user_data;
}

/* Get output */
struct comp_output* comp_server_get_output(struct comp_server* server) {
    if (!server) return NULL;
    return comp_output_manager_get_primary(&server->output_manager);
}

/* View title */
const char* comp_view_get_title(struct comp_view* view) {
    if (!view || !view->xdg_toplevel) return NULL;
    return view->xdg_toplevel->title;
}

/* View geometry */
void comp_view_get_geometry(struct comp_view* view, int32_t* x, int32_t* y,
                            uint32_t* width, uint32_t* height) {
    if (!view) return;
    if (x) *x = view->x;
    if (y) *y = view->y;
    if (view->xdg_toplevel && view->xdg_toplevel->base) {
        /* wlroots 0.19: use current geometry from xdg_surface */
        struct wlr_xdg_surface* surface = view->xdg_toplevel->base;
        if (width) *width = surface->current.geometry.width > 0 ? 
                            surface->current.geometry.width : 
                            (surface->surface ? surface->surface->current.width : 0);
        if (height) *height = surface->current.geometry.height > 0 ?
                              surface->current.geometry.height :
                              (surface->surface ? surface->surface->current.height : 0);
    } else {
        if (width) *width = 0;
        if (height) *height = 0;
    }
}

/* Set view position */
void comp_view_set_position(struct comp_view* view, int32_t x, int32_t y) {
    if (!view) return;
    view->x = x;
    view->y = y;
    if (view->scene_tree) {
        wlr_scene_node_set_position(&view->scene_tree->node, x, y);
    }
}

/* Set view size */
void comp_view_set_size(struct comp_view* view, uint32_t width, uint32_t height) {
    if (!view || !view->xdg_toplevel) return;
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, width, height);
}

/* Close view */
void comp_view_close(struct comp_view* view) {
    if (!view || !view->xdg_toplevel) return;
    wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

/* Check if mapped */
bool comp_view_is_mapped(struct comp_view* view) {
    return view && view->mapped;
}

/* Input forwarding - keyboard */
void comp_server_send_key(struct comp_server* server, uint32_t key, bool pressed) {
    if (!server) return;
    comp_seat_send_key(&server->seat, key, pressed);
}

void comp_server_send_modifiers(struct comp_server* server, uint32_t depressed,
                                 uint32_t latched, uint32_t locked, uint32_t group) {
    if (!server) return;
    comp_seat_send_modifiers(&server->seat, depressed, latched, locked, group);
}

/* Input forwarding - pointer */
void comp_server_send_pointer_motion(struct comp_server* server, double x, double y) {
    if (!server) return;
    comp_seat_send_pointer_motion(&server->seat, x, y);
}

void comp_server_send_pointer_button(struct comp_server* server, uint32_t button, bool pressed) {
    if (!server) return;
    comp_seat_send_pointer_button(&server->seat, button, pressed);
}

void comp_server_send_pointer_axis(struct comp_server* server, bool horizontal, double value) {
    if (!server) return;
    comp_seat_send_pointer_axis(&server->seat, horizontal, value);
}

/* Get view surface dimensions */
void comp_view_get_surface_size(struct comp_view* view, uint32_t* width, uint32_t* height) {
    if (!view || !view->xdg_toplevel || !view->xdg_toplevel->base) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    struct wlr_surface* surface = view->xdg_toplevel->base->surface;
    if (surface) {
        if (width) *width = surface->current.width;
        if (height) *height = surface->current.height;
    } else {
        if (width) *width = 0;
        if (height) *height = 0;
    }
}

/* Render a specific view to buffer (ARGB32 format) */
bool comp_view_render_to_buffer(struct comp_view* view, void* buffer,
                                 uint32_t buf_width, uint32_t buf_height, uint32_t stride) {
    if (!view || !view->mapped || !view->xdg_toplevel || !buffer) {
        return false;
    }
    
    struct wlr_surface* surface = view->xdg_toplevel->base->surface;
    if (!surface) {
        return false;
    }
    
    /* Get the committed buffer from the surface */
    struct wlr_client_buffer* client_buffer = surface->buffer;
    if (!client_buffer) {
        return false;
    }
    
    struct wlr_buffer* wlr_buf = &client_buffer->base;
    
    /* Try to get data pointer via begin_data_ptr_access */
    void* data;
    uint32_t format;
    size_t buf_stride;
    
    if (!wlr_buffer_begin_data_ptr_access(wlr_buf, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                           &data, &format, &buf_stride)) {
        return false;
    }
    
    uint32_t src_width = wlr_buf->width;
    uint32_t src_height = wlr_buf->height;
    
    /* Copy pixels - handle size differences */
    uint32_t copy_width = (src_width < buf_width) ? src_width : buf_width;
    uint32_t copy_height = (src_height < buf_height) ? src_height : buf_height;
    
    uint8_t* dst = (uint8_t*)buffer;
    uint8_t* src = (uint8_t*)data;
    
    for (uint32_t y = 0; y < copy_height; y++) {
        memcpy(dst + y * stride, src + y * buf_stride, copy_width * 4);
    }
    
    wlr_buffer_end_data_ptr_access(wlr_buf);
    
    return true;
}

/* Render full scene to buffer */
bool comp_server_render_frame(struct comp_server* server, void* buffer,
                               uint32_t width, uint32_t height, uint32_t stride) {
    if (!server || !server->scene || !buffer) {
        return false;
    }
    
    struct comp_output* output = comp_output_manager_get_primary(&server->output_manager);
    if (!output || !output->scene_output) {
        return false;
    }
    
    /* Build scene output state */
    struct wlr_scene_output_state_options options = {0};
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    
    if (!wlr_scene_output_build_state(output->scene_output, &state, &options)) {
        wlr_output_state_finish(&state);
        return false;
    }
    
    /* Get the buffer from the state */
    struct wlr_buffer* wlr_buf = state.buffer;
    if (!wlr_buf) {
        wlr_output_state_finish(&state);
        return false;
    }
    
    /* Read pixels from buffer */
    void* data;
    uint32_t format;
    size_t buf_stride;
    
    if (!wlr_buffer_begin_data_ptr_access(wlr_buf, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                           &data, &format, &buf_stride)) {
        wlr_output_state_finish(&state);
        return false;
    }
    
    uint32_t src_width = wlr_buf->width;
    uint32_t src_height = wlr_buf->height;
    uint32_t copy_width = (src_width < width) ? src_width : width;
    uint32_t copy_height = (src_height < height) ? src_height : height;
    
    uint8_t* dst = (uint8_t*)buffer;
    uint8_t* src_ptr = (uint8_t*)data;
    
    for (uint32_t y = 0; y < copy_height; y++) {
        memcpy(dst + y * stride, src_ptr + y * buf_stride, copy_width * 4);
    }
    
    wlr_buffer_end_data_ptr_access(wlr_buf);
    wlr_output_state_finish(&state);
    
    return true;
}

/* --- Internal accessors used by other C modules --- */

struct wl_display* comp_server_get_display(struct comp_server* server) {
    return server ? server->display : NULL;
}

struct wlr_scene* comp_server_get_scene(struct comp_server* server) {
    return server ? server->scene : NULL;
}

struct wlr_renderer* comp_server_get_renderer(struct comp_server* server) {
    return server ? server->renderer : NULL;
}

struct wlr_allocator* comp_server_get_allocator(struct comp_server* server) {
    return server ? server->allocator : NULL;
}

struct comp_seat* comp_server_get_seat(struct comp_server* server) {
    return server ? &server->seat : NULL;
}

struct wl_list* comp_server_get_views(struct comp_server* server) {
    return server ? &server->views : NULL;
}

void comp_server_notify_view_added(struct comp_server* server, struct comp_view* view) {
    if (server && server->view_callback) {
        server->view_callback(server->view_callback_data, view, true);
    }
}

void comp_server_notify_view_removed(struct comp_server* server, struct comp_view* view) {
    if (server && server->view_callback) {
        server->view_callback(server->view_callback_data, view, false);
    }
}
