/*
 * output_handler.c - Output management for wlroots compositor
 * 
 * CRITICAL for nested mode (Wayland/X11 backends):
 * - Outputs are created AFTER backend starts
 * - Backend emits new_output signal
 * - We must respond by configuring and committing the output
 */
#define _POSIX_C_SOURCE 200809L

#include "output_handler.h"
#include "compositor_core.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/util/log.h>

/* External accessors - MUST be declared before use */
extern struct wlr_scene* comp_server_get_scene(struct comp_server* server);
extern struct wlr_renderer* comp_server_get_renderer(struct comp_server* server);
extern struct wlr_allocator* comp_server_get_allocator(struct comp_server* server);
extern struct wl_display* comp_server_get_display(struct comp_server* server);

/* Remove output listeners safely */
static void output_remove_listeners(struct comp_output* output) {
    if (!output->listeners_active) return;
    
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    
    output->listeners_active = false;
}

/* Handle frame event - render and present */
static void handle_output_frame(struct wl_listener* listener, void* data) {
    struct comp_output* output = wl_container_of(listener, output, frame);
    (void)data;
    
    struct wlr_scene* scene = comp_server_get_scene(output->server);
    if (!scene || !output->scene_output) return;
    
    /* Render scene to output */
    wlr_scene_output_commit(output->scene_output, NULL);
    
    /* Get current time for frame callbacks */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(output->scene_output, &now);
}

/* Handle output state request (mode change, etc) */
static void handle_output_request_state(struct wl_listener* listener, void* data) {
    struct comp_output* output = wl_container_of(listener, output, request_state);
    struct wlr_output_event_request_state* event = data;
    
    wlr_output_commit_state(output->wlr_output, event->state);
}

/* Handle output destroy */
static void handle_output_destroy(struct wl_listener* listener, void* data) {
    struct comp_output* output = wl_container_of(listener, output, destroy);
    (void)data;
    
    wlr_log(WLR_INFO, "Output destroyed: %s", output->wlr_output->name);
    
    wl_list_remove(&output->link);
    output_remove_listeners(output);
    
    free(output);
}

/* Handle new output from backend */
static void handle_new_output(struct wl_listener* listener, void* data) {
    struct comp_output_manager* mgr = wl_container_of(listener, mgr, new_output);
    struct wlr_output* wlr_output = data;
    
    wlr_log(WLR_INFO, "New output: %s (%s)", wlr_output->name, 
            wlr_output->description ? wlr_output->description : "no description");
    
    /* Initialize renderer for this output */
    struct wlr_renderer* renderer = comp_server_get_renderer(mgr->server);
    struct wlr_allocator* allocator = comp_server_get_allocator(mgr->server);
    
    if (!wlr_output_init_render(wlr_output, allocator, renderer)) {
        wlr_log(WLR_ERROR, "Failed to init output render");
        return;
    }
    
    /* Create output state */
    struct comp_output* output = calloc(1, sizeof(struct comp_output));
    if (!output) {
        wlr_log(WLR_ERROR, "Failed to allocate output");
        return;
    }
    
    output->server = mgr->server;
    output->wlr_output = wlr_output;
    wlr_output->data = output;
    
    /* Configure output - prefer first available mode */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
        output->width = mode->width;
        output->height = mode->height;
        wlr_log(WLR_INFO, "Output mode: %dx%d@%dmHz", 
                mode->width, mode->height, mode->refresh);
    } else {
        /* Fallback for headless/nested backends without modes */
        output->width = 1280;
        output->height = 720;
        wlr_output_state_set_custom_mode(&state, 1280, 720, 0);
        wlr_log(WLR_INFO, "Output using custom mode: 1280x720");
    }
    
    /* Commit output state */
    if (!wlr_output_commit_state(wlr_output, &state)) {
        wlr_log(WLR_ERROR, "Failed to commit output state");
        wlr_output_state_finish(&state);
        free(output);
        return;
    }
    wlr_output_state_finish(&state);
    
    /* Setup listeners */
    output->frame.notify = handle_output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    
    output->request_state.notify = handle_output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    
    output->destroy.notify = handle_output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    
    output->listeners_active = true;
    
    /* Add to output layout */
    struct wlr_output_layout_output* l_output = 
        wlr_output_layout_add_auto(mgr->layout, wlr_output);
    if (!l_output) {
        wlr_log(WLR_ERROR, "Failed to add output to layout");
    }
    
    /* Create scene output */
    struct wlr_scene* scene = comp_server_get_scene(mgr->server);
    output->scene_output = wlr_scene_output_create(scene, wlr_output);
    if (!output->scene_output) {
        wlr_log(WLR_ERROR, "Failed to create scene output");
    }
    
    /* Add to output list */
    wl_list_insert(&mgr->outputs, &output->link);
    
    wlr_log(WLR_INFO, "Output configured successfully: %dx%d", 
            output->width, output->height);
}

/* Initialize output manager */
bool comp_output_manager_init(struct comp_output_manager* mgr, struct comp_server* server) {
    memset(mgr, 0, sizeof(*mgr));
    mgr->server = server;
    
    wl_list_init(&mgr->outputs);
    
    /* Create output layout */
    mgr->layout = wlr_output_layout_create(comp_server_get_display(server));
    if (!mgr->layout) {
        wlr_log(WLR_ERROR, "Failed to create output layout");
        return false;
    }
    
    /* Listen for new outputs - backend must be created first */
    /* Note: This listener is connected to backend in comp_server_init_backend */
    
    wlr_log(WLR_INFO, "Output manager initialized");
    return true;
}

/* Connect to backend for output events */
void comp_output_manager_connect_backend(struct comp_output_manager* mgr, 
                                          struct wlr_backend* backend) {
    mgr->new_output.notify = handle_new_output;
    wl_signal_add(&backend->events.new_output, &mgr->new_output);
}

/* Cleanup output manager */
void comp_output_manager_finish(struct comp_output_manager* mgr) {
    if (!mgr) return;
    
    wl_list_remove(&mgr->new_output.link);
    
    /* Outputs will be cleaned up by wlr_output destruction */
    
    if (mgr->layout) {
        wlr_output_layout_destroy(mgr->layout);
        mgr->layout = NULL;
    }
}

/* Get primary output */
struct comp_output* comp_output_manager_get_primary(struct comp_output_manager* mgr) {
    if (!mgr || wl_list_empty(&mgr->outputs)) return NULL;
    
    struct comp_output* output;
    output = wl_container_of(mgr->outputs.next, output, link);
    return output;
}

/* Get output size */
void comp_output_get_size(struct comp_output* output, uint32_t* width, uint32_t* height) {
    if (!output) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    if (width) *width = output->width;
    if (height) *height = output->height;
}

/* Manually trigger frame rendering - needed for headless backend */
void comp_output_render_frame(struct comp_output* output) {
    if (!output || !output->scene_output || !output->wlr_output) return;
    
    struct wlr_scene* scene = comp_server_get_scene(output->server);
    if (!scene) return;
    
    /* Commit the scene to output */
    if (wlr_scene_output_commit(output->scene_output, NULL)) {
        /* Send frame done to all surfaces so clients render next frame */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(output->scene_output, &now);
    }
}
