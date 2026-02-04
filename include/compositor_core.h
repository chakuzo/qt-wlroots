/*
 * compositor_core.h - C wrapper for wlroots (MIT License compatible)
 * 
 * IMPORTANT: All functions use "comp_" prefix to avoid conflicts with wlr_*
 */
#ifndef COMPOSITOR_CORE_H
#define COMPOSITOR_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

/* Forward declarations - opaque types for C++ */
struct comp_server;
struct comp_output;
struct comp_view;

/* Callback types for Qt integration */
typedef void (*comp_frame_callback_t)(void* user_data, uint32_t width, uint32_t height, void* buffer);
typedef void (*comp_view_callback_t)(void* user_data, struct comp_view* view, bool added);
typedef void (*comp_commit_callback_t)(void* user_data);

/* Server lifecycle */
struct comp_server* comp_server_create(void);
void comp_server_destroy(struct comp_server* server);

/* Initialize backend - MUST be called before start */
bool comp_server_init_backend(struct comp_server* server);

/* Start server - creates socket, starts event loop */
bool comp_server_start(struct comp_server* server);

/* Get wayland display socket name */
const char* comp_server_get_socket(struct comp_server* server);

/* Event loop integration - returns fd for external polling */
int comp_server_get_event_fd(struct comp_server* server);

/* Process pending events - call when fd is readable */
void comp_server_dispatch_events(struct comp_server* server);

/* Flush clients */
void comp_server_flush_clients(struct comp_server* server);

/* Set frame callback - called when compositor has new frame */
void comp_server_set_frame_callback(struct comp_server* server, 
                                     comp_frame_callback_t callback,
                                     void* user_data);

/* Set view callback - called when views are added/removed */
void comp_server_set_view_callback(struct comp_server* server,
                                    comp_view_callback_t callback,
                                    void* user_data);

/* Set commit callback - called when client commits new content */
void comp_server_set_commit_callback(struct comp_server* server,
                                      comp_commit_callback_t callback,
                                      void* user_data);

/* Notify frame commit - called internally when clients commit */
void comp_server_notify_frame_commit(struct comp_server* server);

/* Output management */
struct comp_output* comp_server_get_output(struct comp_server* server);
void comp_output_get_size(struct comp_output* output, uint32_t* width, uint32_t* height);

/* View management */
const char* comp_view_get_title(struct comp_view* view);
void comp_view_get_geometry(struct comp_view* view, int32_t* x, int32_t* y, 
                            uint32_t* width, uint32_t* height);
void comp_view_set_position(struct comp_view* view, int32_t x, int32_t y);
void comp_view_set_size(struct comp_view* view, uint32_t width, uint32_t height);
void comp_view_request_size(struct comp_view* view, uint32_t width, uint32_t height);
void comp_view_focus(struct comp_view* view);
void comp_view_close(struct comp_view* view);
bool comp_view_is_mapped(struct comp_view* view);

/* Input - keyboard */
void comp_server_send_key(struct comp_server* server, uint32_t key, bool pressed);
void comp_server_send_modifiers(struct comp_server* server, uint32_t mods_depressed,
                                 uint32_t mods_latched, uint32_t mods_locked, uint32_t group);

/* Input - pointer */
void comp_server_send_pointer_motion(struct comp_server* server, double x, double y);
void comp_server_send_pointer_button(struct comp_server* server, uint32_t button, bool pressed);
void comp_server_send_pointer_axis(struct comp_server* server, bool horizontal, double value);

/* Rendering - get current frame buffer */
bool comp_server_render_frame(struct comp_server* server, void* buffer, 
                               uint32_t width, uint32_t height, uint32_t stride);

/* Trigger frame render and notify clients - call regularly from Qt timer */
void comp_server_render_and_notify(struct comp_server* server);

/* Render a specific view to buffer (ARGB32 format) */
bool comp_view_render_to_buffer(struct comp_view* view, void* buffer,
                                 uint32_t width, uint32_t height, uint32_t stride);

/* Get view surface dimensions */
void comp_view_get_surface_size(struct comp_view* view, uint32_t* width, uint32_t* height);

#ifdef __cplusplus
}
#endif

#endif /* COMPOSITOR_CORE_H */
