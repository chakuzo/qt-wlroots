/*
 * output_handler.h - Output management
 * 
 * CRITICAL for nested mode: Output must be created AFTER backend starts!
 */
#ifndef OUTPUT_HANDLER_H
#define OUTPUT_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

struct comp_server;

/* Output state */
struct comp_output {
    struct wl_list link;  /* comp_server.outputs */
    struct comp_server* server;
    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;
    
    /* Dimensions */
    uint32_t width;
    uint32_t height;
    
    /* Frame timing */
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
    
    bool listeners_active;
};

/* Output manager */
struct comp_output_manager {
    struct comp_server* server;
    struct wlr_output_layout* layout;
    struct wl_list outputs;  /* comp_output.link */
    
    struct wl_listener new_output;
};

/* Initialize output manager */
bool comp_output_manager_init(struct comp_output_manager* mgr, struct comp_server* server);

/* Cleanup */
void comp_output_manager_finish(struct comp_output_manager* mgr);

/* Get primary output */
struct comp_output* comp_output_manager_get_primary(struct comp_output_manager* mgr);

/* Output operations */
void comp_output_get_size(struct comp_output* output, uint32_t* width, uint32_t* height);

/* Manually trigger frame rendering - needed for headless backend */
void comp_output_render_frame(struct comp_output* output);

#ifdef __cplusplus
}
#endif

#endif /* OUTPUT_HANDLER_H */
