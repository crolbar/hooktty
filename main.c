#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>

#include "main.h"
#include "registry.c"
#include "sys/mman.h"
#include "util.c"
#include "xdg-shell.c"

static int keep_running = 1;

static void
buffer_release(void* data, struct wl_buffer* buffer)
{
    struct buffer* buff = data;
    buff->busy = 0;
}

static struct buffer*
get_free_buff(struct state* state)
{
    struct buffer* buff = NULL;
    if (!state->buff1->busy)
        buff = state->buff1;

    if (!state->buff2->busy)
        buff = state->buff2;

    if (buff == NULL) {
        printf("both buffers are busy\n");
        exit(1);
    }

    return buff;
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void
paint_data(struct state* state, struct buffer* buff, uint32_t time)
{
    int height = state->height;
    int width = state->width;

    uint32_t* data =
      state->shm_data +
      buff->offset / 4; // offset is byte offset we don't need bytes

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data[y * width + x] =
              (((time / 1000) & 1) == 0) ? 0x000FF000 : 0x00FF0000;
        }
    }
}

static void
new_buffers(struct state* state)
{
    int height = state->height;
    int width = state->width;
    int fd, stride, size;

    stride = width * 4;
    size = stride * height * 2;
    state->size = size;

    fd = allocate_shm_file(size);

    if (fd < 0) {
        fprintf(stderr, "creating a buffer file for %d B failed\n", size);
        return;
    }

    state->shm_data =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (state->shm_data == MAP_FAILED) {
        close(fd);
        return;
    }

    struct wl_shm_pool* pool;
    pool = wl_shm_create_pool(state->shm, fd, size);

    state->buff1 = malloc(sizeof(*state->buff1));
    state->buff1->busy = 0;

    state->buff2 = malloc(sizeof(*state->buff2));
    state->buff2->busy = 0;

    state->buff1->buffer = wl_shm_pool_create_buffer(
      pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    state->buff1->offset = 0;

    int offset = height * stride * 1;
    state->buff2->offset = offset;
    state->buff2->buffer = wl_shm_pool_create_buffer(
      pool, offset, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    wl_buffer_add_listener(
      state->buff1->buffer, &buffer_listener, state->buff1);
    wl_buffer_add_listener(
      state->buff2->buffer, &buffer_listener, state->buff2);
}

static void
redraw(void* data, struct wl_callback* callback, uint32_t time);

static const struct wl_callback_listener frame_listener;

static void
redraw(void* data, struct wl_callback* callback, uint32_t time)
{
    struct state* state = data;
    struct buffer* buffer = get_free_buff(state);

    if (buffer->offset != 0) {
        printf("redraw buff 2\n");
    } else if (buffer->offset == 0) {
        printf("redraw buff 1\n");
    }

    uint32_t d = time - state->last_frame_time;
    if (d > 0)
        state->fps = 1000 / d;

    printf("fps: %d\n", state->fps);

    state->last_frame_time = time;
    state->frame_count++;

    wl_callback_destroy(callback);
    state->frame_callback = NULL;

    paint_data(state, buffer, time);

    wl_surface_attach(state->surface, buffer->buffer, 0, 0);
    wl_surface_damage(state->surface, 0, 0, state->width, state->height);

    state->frame_callback = wl_surface_frame(state->surface);
    wl_callback_add_listener(state->frame_callback, &frame_listener, state);
    wl_surface_commit(state->surface);

    buffer->busy = 1;
}

static const struct wl_callback_listener frame_listener = { redraw };

static void
signal_int(int signum)
{
    keep_running = 0;
}

int
main(int argc, char* argv[])
{
    struct sigaction sigint;
    sigint.sa_handler = signal_int;
    sigemptyset(&sigint.sa_mask);
    sigint.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &sigint, NULL);

    struct state* state;
    state = malloc(sizeof(*state));
    state->width = 200;
    state->height = 200;
    state->last_frame_time = 0;
    state->frame_count = 0;

    state->display = wl_display_connect(NULL);
    if (!state->display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return 1;
    }
    printf("Connection established!\n");

    state->registry = wl_display_get_registry(state->display);
    wl_registry_add_listener(state->registry, &registry_listener, state);

    wl_display_roundtrip(state->display);
    if (state->shm == NULL) {
        fprintf(stderr, "No wl_shm global\n");
        exit(1);
    }

    state->surface = wl_compositor_create_surface(state->compositor);

    setup_xdg_shell(state);

    new_buffers(state);

    state->frame_callback = wl_surface_frame(state->surface);
    wl_callback_add_listener(state->frame_callback, &frame_listener, state);

    wl_surface_attach(state->surface, state->buff1->buffer, 0, 0);
    wl_surface_damage(state->surface, 0, 0, state->width, state->height);
    wl_surface_commit(state->surface);

    while (wl_display_dispatch(state->display) != -1 && keep_running) {
    }

    // TODO free all
    wl_display_disconnect(state->display);
    free(state);
    return 0;
}
