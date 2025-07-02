#include "main.h"
#include "xdg-shell-client-protocol.h"

static void
handle_xdg_toplevel_configure(void* data,
                              struct xdg_toplevel* xdg_toplevel,
                              int32_t width,
                              int32_t height,
                              struct wl_array* states)
{
}

static void
handle_xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel)
{
    keep_running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    handle_xdg_toplevel_configure,
    handle_xdg_toplevel_close,
};

static void
handle_xdg_surface_configure(void* data,
                             struct xdg_surface* surface,
                             uint32_t serial)
{
    struct state* state = data;

    xdg_surface_ack_configure(surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    handle_xdg_surface_configure,
};

void
setup_xdg_shell(struct state* state)
{
    state->xdg_surface =
      xdg_wm_base_get_xdg_surface(state->wm_base, state->surface);
    xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);

    state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
    xdg_toplevel_add_listener(
      state->xdg_toplevel, &xdg_toplevel_listener, state);
    xdg_toplevel_set_title(state->xdg_toplevel, "some title");
    xdg_toplevel_set_app_id(state->xdg_toplevel, "some app id");
}
