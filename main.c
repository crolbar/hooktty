#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>

#include "main.h"
#include "registry.c"
#include "sys/mman.h"
#include "util.c"

static int keep_running = 1;

struct wl_surface* surface;
struct display* display;
struct xdg_surface* xdg_surface;
struct xdg_toplevel* xdg_toplevel;
struct wl_shm_pool* pool;
struct wl_buffer* buffer;

int width, height;

static void
signal_int(int signum)
{
    keep_running = 0;
}

// static void
// buffer_release(void* data, struct wl_buffer* buffer)
//{
//     wl_buffer_destroy(buffer);
// }
// static const struct wl_buffer_listener buffer_listener = { buffer_release };

// static void
// handle_xdg_surface_configure(void* data,
//                              struct xdg_surface* surface,
//                              uint32_t serial)
//{
//     // struct window *window = data;
//
//     xdg_surface_ack_configure(surface, serial);
//
//     // if (window->wait_for_configure) {
//     //	redraw(window, NULL, 0);
//     //	window->wait_for_configure = false;
//     // }
// }
//
// static const struct xdg_surface_listener xdg_surface_listener = {
//     handle_xdg_surface_configure,
// };

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

int
main(int argc, char* argv[])
{
    struct sigaction sigint;
    sigint.sa_handler = signal_int;
    sigemptyset(&sigint.sa_mask);
    sigint.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &sigint, NULL);

    display = malloc(sizeof(struct display*));

    display->display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return 1;
    }
    fprintf(stderr, "Connection established!\n");

    display->registry = wl_display_get_registry(display->display);
    wl_registry_add_listener(display->registry, &registry_listener, display);
    wl_display_roundtrip(display->display);

    surface = wl_compositor_create_surface(display->compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(display->wm_base, surface);
    // xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    // xdg_toplevel_set_title(xdg_toplevel, "some title");
    // xdg_toplevel_set_app_id(xdg_toplevel, "some app id");

    width = 200;
    height = 200;

    int fd, size, stride;

    stride = width * 4;
    size = stride * height;

    fd = allocate_shm_file(size);

    if (fd < 0) {
        fprintf(
          stderr, "creating a buffer file for %d B failed: %d\n", size, stride);
        return -1;
    }

    uint32_t* data =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (data == MAP_FAILED) {
        close(fd);
        return 1;
    }

    pool = wl_shm_create_pool(display->shm, fd, size);
    buffer = wl_shm_pool_create_buffer(
      pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data[y * width + x] = 0x00FF0000;
        }
    }

    munmap(data, size);
    // wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    wl_surface_attach(surface, buffer, 0, 0);
    // wl_surface_damage(surface, 0, 0, width, height);
    wl_surface_commit(surface);

    while (wl_display_dispatch(display->display) != -1 && keep_running) {
    }

    wl_display_disconnect(display->display);
    return 0;
}
