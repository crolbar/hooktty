#ifndef MAIN
#define MAIN

#include <stddef.h>
#include <stdint.h>

static int keep_running;

struct state {
    struct wl_display* display;
    struct wl_registry* registry;

    struct wl_compositor* compositor;
    struct xdg_wm_base* wm_base;
	struct wl_shm *shm;

    //struct wl_seat* seat;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

    struct buffer* buff1;
    struct buffer* buff2;

	struct wl_callback *frame_callback;

	int width, height;

    size_t size;
    uint32_t* shm_data;



    uint32_t last_frame_time;
    uint32_t frame_count;
    uint32_t fps;
};

struct buffer {
    struct wl_buffer* buffer;
    int busy;
    int offset;
};


static void
redraw(void* data, struct wl_callback* callback, uint32_t time);

#endif
