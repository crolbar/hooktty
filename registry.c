#include "xdg-shell-client-protocol.h"
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

#include "main.h"

static void
xdg_wm_base_ping(void* data, struct xdg_wm_base* shell, uint32_t serial)
{
    xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    xdg_wm_base_ping,
};

static void
registry_global(void* data,
                struct wl_registry* wl_registry,
                uint32_t name,
                const char* interface,
                uint32_t version)
{
    struct display* d = data;

    printf(
      "interface: '%s', version: %d, name: %d\n", interface, version, name);

    if (strcmp(interface, "wl_compositor") == 0) {
        d->compositor =
          wl_registry_bind(wl_registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        d->wm_base =
          wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(d->wm_base, &xdg_wm_base_listener, d);
    } else if (strcmp(interface, "wl_seat") == 0) {
        // d->seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 1);
        // wl_seat_add_listener(d->seat, &seat_listener, d);
    } else if (strcmp(interface, "wl_shm") == 0) {
        d->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
        //wl_shm_add_listener(d->shm, &shm_listener, d);
    }
}

static void
registry_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};
