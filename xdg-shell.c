#include "macros.h"
#include "main.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-shell.h"

void
handle_xdg_wm_base_ping(void* data, struct xdg_wm_base* shell, uint32_t serial)
{
    xdg_wm_base_pong(shell, serial);
}

void
handle_xdg_toplevel_configure_bounds(void* data,
                                     struct xdg_toplevel* xdg_toplevel,
                                     int32_t width,
                                     int32_t height)
{
}

void
handle_xdg_toplevel_wm_capabilities(void* data,
                                    struct xdg_toplevel* xdg_toplevel,
                                    struct wl_array* capabilities)
{
}

void
handle_xdg_toplevel_configure(void* data,
                              struct xdg_toplevel* xdg_toplevel,
                              int32_t width,
                              int32_t height,
                              struct wl_array* states)
{
    struct state* state = data;
    HOG("toplevel configure w: %d, h: %d", width, height);

    if (width != 0 && height != 0) {
        state->width = width;
        state->height = height;
        state->window_resized = true;
        state->needs_redraw = true;
    }
}

void
handle_xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel)
{
    struct state* state = data;
    state->keep_running = 0;
}

void
handle_xdg_surface_configure(void* data,
                             struct xdg_surface* surface,
                             uint32_t serial)
{
    struct state* state = data;

    xdg_surface_ack_configure(surface, serial);

    if (state->buff1 == NULL || state->buff2 == NULL) {
        new_buffers(state);
        update_grid(state);

        state->frame_callback = wl_surface_frame(state->surface);
        wl_callback_add_listener(state->frame_callback, &frame_listener, state);

        wl_surface_attach(state->surface, state->buff1->buffer, 0, 0);
        wl_surface_damage(state->surface,
                          0,
                          0,
                          state->width * state->output_scale_factor,
                          state->height * state->output_scale_factor);
        wl_surface_commit(state->surface);
    }
}

void
setup_xdg_shell(struct state* state)
{
    state->xdg_surface =
      xdg_wm_base_get_xdg_surface(state->wm_base, state->surface);
    xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);

    state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
    xdg_toplevel_add_listener(
      state->xdg_toplevel, &xdg_toplevel_listener, state);
    xdg_toplevel_set_title(state->xdg_toplevel, "hooktty");
    xdg_toplevel_set_app_id(state->xdg_toplevel, "hooktty");
}
