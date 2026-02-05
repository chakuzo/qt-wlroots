/*
 * render_backend.h - Abstraction layer for software/hardware rendering
 *
 * Provides a unified interface for different rendering backends:
 * - Software (Pixman): CPU-based, works everywhere, easier debugging
 * - Hardware (GLES2 + DMA-BUF): GPU-accelerated, zero-copy to Qt
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024
 */
#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

/* Forward declarations */
struct comp_server;
struct comp_view;
struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;

/* Renderer type */
typedef enum {
    RENDER_BACKEND_SOFTWARE,  /* Pixman - CPU rendering */
    RENDER_BACKEND_HARDWARE,  /* GLES2 + DMA-BUF - GPU rendering */
} render_backend_type_t;

/* Render backend state */
struct render_backend {
    render_backend_type_t type;
    struct wlr_backend* wlr_backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    
    /* For hardware backend: DMA-BUF file descriptors */
    int dmabuf_fd;
    uint32_t dmabuf_stride;
    uint32_t dmabuf_width;
    uint32_t dmabuf_height;
    uint32_t dmabuf_format;
    
    /* For software backend: pixel buffer */
    void* pixel_buffer;
    uint32_t buffer_size;
};

/* Create render backend */
struct render_backend* render_backend_create(render_backend_type_t type,
                                              struct wl_event_loop* event_loop);

/* Destroy render backend */
void render_backend_destroy(struct render_backend* backend);

/* Initialize renderer and allocator */
bool render_backend_init_renderer(struct render_backend* backend,
                                   struct wl_display* display);

/* Get the wlr_backend (for creating outputs, etc.) */
struct wlr_backend* render_backend_get_wlr_backend(struct render_backend* backend);

/* Get the renderer */
struct wlr_renderer* render_backend_get_renderer(struct render_backend* backend);

/* Get the allocator */
struct wlr_allocator* render_backend_get_allocator(struct render_backend* backend);

/* Render a frame and get the result
 * For software: copies pixels to buffer
 * For hardware: returns DMA-BUF fd */
bool render_backend_capture_frame(struct render_backend* backend,
                                   void* scene_output,
                                   void** buffer_out,
                                   int* fd_out,
                                   uint32_t* width_out,
                                   uint32_t* height_out,
                                   uint32_t* stride_out,
                                   uint32_t* format_out);

/* Check if hardware acceleration is available */
bool render_backend_hardware_available(void);

/* Get backend type name */
const char* render_backend_type_name(render_backend_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* RENDER_BACKEND_H */
