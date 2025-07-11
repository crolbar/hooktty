#pragma once

#include "main.h"
#include "xdg-shell-client-protocol.h"

void
handle_xdg_wm_base_ping(void* data, struct xdg_wm_base* shell, uint32_t serial);

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    handle_xdg_wm_base_ping
};

void
handle_xdg_toplevel_configure(void* data,
                              struct xdg_toplevel* xdg_toplevel,
                              int32_t width,
                              int32_t height,
                              struct wl_array* states);

void
handle_xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel);

void
handle_xdg_toplevel_configure_bounds(void* data,
                                     struct xdg_toplevel* xdg_toplevel,
                                     int32_t width,
                                     int32_t height);

void
handle_xdg_toplevel_wm_capabilities(void* data,
                                    struct xdg_toplevel* xdg_toplevel,
                                    struct wl_array* capabilities);

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    handle_xdg_toplevel_configure,
    handle_xdg_toplevel_close,
    handle_xdg_toplevel_configure_bounds,
    handle_xdg_toplevel_wm_capabilities,
};

void
handle_xdg_surface_configure(void* data,
                             struct xdg_surface* surface,
                             uint32_t serial);

static const struct xdg_surface_listener xdg_surface_listener = {
    handle_xdg_surface_configure,
};

void
setup_xdg_shell(struct state* state);
