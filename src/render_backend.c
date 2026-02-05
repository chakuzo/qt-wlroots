/*
 * render_backend.c - Software/Hardware rendering backend implementation
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024
 */
#define _POSIX_C_SOURCE 200809L

#include "render_backend.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <drm_fourcc.h>

/* Try to include DMA-BUF and GLES2 headers if available */
#ifdef WLR_HAS_GLES2_RENDERER
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#endif

/* Check if hardware acceleration is available at runtime */
bool render_backend_hardware_available(void) {
#ifdef WLR_HAS_GLES2_RENDERER
    /* Check for EGL/DRM availability */
    /* This is a simplified check - real implementation would probe GPU */
    const char* session_type = getenv("XDG_SESSION_TYPE");
    if (session_type && strcmp(session_type, "wayland") == 0) {
        return true;
    }
    if (getenv("DISPLAY") != NULL) {
        return true;  /* X11 with EGL might work */
    }
    return false;
#else
    return false;
#endif
}

const char* render_backend_type_name(render_backend_type_t type) {
    switch (type) {
        case RENDER_BACKEND_SOFTWARE:
            return "Software (Pixman)";
        case RENDER_BACKEND_HARDWARE:
            return "Hardware (GLES2)";
        default:
            return "Unknown";
    }
}

struct render_backend* render_backend_create(render_backend_type_t type,
                                              struct wl_event_loop* event_loop) {
    struct render_backend* backend = calloc(1, sizeof(struct render_backend));
    if (!backend) {
        wlr_log(WLR_ERROR, "Failed to allocate render backend");
        return NULL;
    }
    
    backend->type = type;
    backend->dmabuf_fd = -1;
    
    /* Create wlr_backend - always headless for embedding */
    backend->wlr_backend = wlr_headless_backend_create(event_loop);
    if (!backend->wlr_backend) {
        wlr_log(WLR_ERROR, "Failed to create headless backend");
        free(backend);
        return NULL;
    }
    
    wlr_log(WLR_INFO, "Created render backend: %s", render_backend_type_name(type));
    return backend;
}

bool render_backend_init_renderer(struct render_backend* backend,
                                   struct wl_display* display) {
    if (!backend || !backend->wlr_backend) return false;
    
    switch (backend->type) {
        case RENDER_BACKEND_SOFTWARE:
            /* Pixman software renderer */
            backend->renderer = wlr_pixman_renderer_create();
            if (!backend->renderer) {
                wlr_log(WLR_ERROR, "Failed to create pixman renderer");
                return false;
            }
            wlr_log(WLR_INFO, "Using Pixman software renderer");
            break;
            
        case RENDER_BACKEND_HARDWARE:
#ifdef WLR_HAS_GLES2_RENDERER
            /* Try GLES2 renderer with EGL */
            backend->renderer = wlr_renderer_autocreate(backend->wlr_backend);
            if (!backend->renderer) {
                wlr_log(WLR_ERROR, "Failed to create hardware renderer, falling back to software");
                backend->type = RENDER_BACKEND_SOFTWARE;
                backend->renderer = wlr_pixman_renderer_create();
                if (!backend->renderer) {
                    wlr_log(WLR_ERROR, "Failed to create fallback pixman renderer");
                    return false;
                }
            } else {
                wlr_log(WLR_INFO, "Using hardware-accelerated renderer");
            }
#else
            wlr_log(WLR_INFO, "Hardware rendering not available, using software");
            backend->type = RENDER_BACKEND_SOFTWARE;
            backend->renderer = wlr_pixman_renderer_create();
            if (!backend->renderer) {
                return false;
            }
#endif
            break;
            
        default:
            wlr_log(WLR_ERROR, "Unknown render backend type");
            return false;
    }
    
    /* Initialize renderer with display */
    wlr_renderer_init_wl_display(backend->renderer, display);
    
    /* Create allocator */
    backend->allocator = wlr_allocator_autocreate(backend->wlr_backend, 
                                                   backend->renderer);
    if (!backend->allocator) {
        wlr_log(WLR_ERROR, "Failed to create allocator");
        return false;
    }
    
    wlr_log(WLR_INFO, "Render backend initialized: %s", 
            render_backend_type_name(backend->type));
    return true;
}

void render_backend_destroy(struct render_backend* backend) {
    if (!backend) return;
    
    if (backend->pixel_buffer) {
        free(backend->pixel_buffer);
    }
    
    if (backend->dmabuf_fd >= 0) {
        close(backend->dmabuf_fd);
    }
    
    if (backend->renderer) {
        wlr_renderer_destroy(backend->renderer);
    }
    
    /* Backend is destroyed by wl_display cleanup */
    
    free(backend);
}

struct wlr_backend* render_backend_get_wlr_backend(struct render_backend* backend) {
    return backend ? backend->wlr_backend : NULL;
}

struct wlr_renderer* render_backend_get_renderer(struct render_backend* backend) {
    return backend ? backend->renderer : NULL;
}

struct wlr_allocator* render_backend_get_allocator(struct render_backend* backend) {
    return backend ? backend->allocator : NULL;
}

bool render_backend_capture_frame(struct render_backend* backend,
                                   void* scene_output_ptr,
                                   void** buffer_out,
                                   int* fd_out,
                                   uint32_t* width_out,
                                   uint32_t* height_out,
                                   uint32_t* stride_out,
                                   uint32_t* format_out) {
    if (!backend || !scene_output_ptr) return false;
    
    struct wlr_scene_output* scene_output = scene_output_ptr;
    
    /* Build the scene state */
    struct wlr_scene_output_state_options options = {0};
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    
    if (!wlr_scene_output_build_state(scene_output, &state, &options)) {
        wlr_output_state_finish(&state);
        return false;
    }
    
    struct wlr_buffer* buffer = state.buffer;
    if (!buffer) {
        wlr_output_state_finish(&state);
        return false;
    }
    
    uint32_t width = buffer->width;
    uint32_t height = buffer->height;
    
    if (width_out) *width_out = width;
    if (height_out) *height_out = height;
    
    switch (backend->type) {
        case RENDER_BACKEND_SOFTWARE: {
            /* Software path: read pixels into CPU buffer */
            void* data;
            uint32_t format;
            size_t buf_stride;
            
            if (!wlr_buffer_begin_data_ptr_access(buffer, 
                    WLR_BUFFER_DATA_PTR_ACCESS_READ,
                    &data, &format, &buf_stride)) {
                wlr_output_state_finish(&state);
                return false;
            }
            
            /* Allocate/reallocate pixel buffer if needed */
            uint32_t needed_size = buf_stride * height;
            if (backend->buffer_size < needed_size) {
                free(backend->pixel_buffer);
                backend->pixel_buffer = malloc(needed_size);
                backend->buffer_size = needed_size;
            }
            
            if (backend->pixel_buffer) {
                memcpy(backend->pixel_buffer, data, needed_size);
            }
            
            wlr_buffer_end_data_ptr_access(buffer);
            wlr_output_state_finish(&state);
            
            if (buffer_out) *buffer_out = backend->pixel_buffer;
            if (fd_out) *fd_out = -1;
            if (stride_out) *stride_out = (uint32_t)buf_stride;
            if (format_out) *format_out = format;
            
            return backend->pixel_buffer != NULL;
        }
        
        case RENDER_BACKEND_HARDWARE: {
            /* Hardware path: try to get DMA-BUF fd */
            struct wlr_dmabuf_attributes dmabuf;
            
            if (wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
                /* Got DMA-BUF - can do zero-copy */
                if (backend->dmabuf_fd >= 0 && backend->dmabuf_fd != dmabuf.fd[0]) {
                    close(backend->dmabuf_fd);
                }
                
                /* Duplicate the fd so we own it */
                backend->dmabuf_fd = dup(dmabuf.fd[0]);
                backend->dmabuf_width = dmabuf.width;
                backend->dmabuf_height = dmabuf.height;
                backend->dmabuf_stride = dmabuf.stride[0];
                backend->dmabuf_format = dmabuf.format;
                
                wlr_output_state_finish(&state);
                
                if (buffer_out) *buffer_out = NULL;
                if (fd_out) *fd_out = backend->dmabuf_fd;
                if (stride_out) *stride_out = backend->dmabuf_stride;
                if (format_out) *format_out = backend->dmabuf_format;
                
                return true;
            }
            
            /* Fallback to data pointer access if DMA-BUF not available */
            void* data;
            uint32_t format;
            size_t buf_stride;
            
            if (!wlr_buffer_begin_data_ptr_access(buffer,
                    WLR_BUFFER_DATA_PTR_ACCESS_READ,
                    &data, &format, &buf_stride)) {
                wlr_output_state_finish(&state);
                return false;
            }
            
            uint32_t needed_size = buf_stride * height;
            if (backend->buffer_size < needed_size) {
                free(backend->pixel_buffer);
                backend->pixel_buffer = malloc(needed_size);
                backend->buffer_size = needed_size;
            }
            
            if (backend->pixel_buffer) {
                memcpy(backend->pixel_buffer, data, needed_size);
            }
            
            wlr_buffer_end_data_ptr_access(buffer);
            wlr_output_state_finish(&state);
            
            if (buffer_out) *buffer_out = backend->pixel_buffer;
            if (fd_out) *fd_out = -1;
            if (stride_out) *stride_out = (uint32_t)buf_stride;
            if (format_out) *format_out = format;
            
            return backend->pixel_buffer != NULL;
        }
        
        default:
            wlr_output_state_finish(&state);
            return false;
    }
}
