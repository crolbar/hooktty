#ifndef MAIN
#define MAIN

#include <stdint.h>
#include <stdbool.h>
#include <wayland-util.h>

struct display
{
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct xdg_wm_base* wm_base;
    struct wl_seat* seat;
    struct wl_keyboard* keyboard;
	struct wl_shm *shm;
	const struct format *format;
	bool paint_format;
	bool has_format;
};

struct buffer {
	struct window *window;
	struct wl_buffer *buffer;
	void *shm_data;
	int busy;
	int width, height;
	size_t size;	/* width * 3 * height */
	struct wl_list buffer_link; /** window::buffer_list */
};


#endif
