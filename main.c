#include <signal.h>
#include <wayland-client.h>

#include "macros.h"
#include "main.h"
#include "registry.c"
#include "util.c"
#include "xdg-shell.h"

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
        HOG_ERR("Both buffers are busy.");
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

    uint32_t* pixel = data;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            *pixel++ = (x - time / 16) * 0x0080401;
        }
    }
}

void
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
        // TODO: handle this
        HOG_ERR("Creating a buffer file for %d B failed", size);
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
check_buffs(struct state* state)
{
    if (state->need_update_buffs) {
        if (state->buff1) {
            if (state->buff1->buffer)
                wl_buffer_destroy(state->buff1->buffer);

            free(state->buff1);
        }

        if (state->buff2) {
            if (state->buff2->buffer)
                wl_buffer_destroy(state->buff2->buffer);

            free(state->buff2);
        }

        munmap(state->shm_data, state->size);

        new_buffers(state);

        state->need_update_buffs = 0;
    }
}

void
redraw(void* data, struct wl_callback* callback, uint32_t time)
{
    struct state* state = data;
    check_buffs(state);
    struct buffer* buffer = get_free_buff(state);

    wl_callback_destroy(callback);
    state->frame_callback = NULL;

    paint_data(state, buffer, time);

    wl_surface_attach(state->surface, buffer->buffer, 0, 0);
    wl_surface_damage(state->surface, 0, 0, state->width, state->height);

    state->frame_callback = wl_surface_frame(state->surface);
    wl_callback_add_listener(state->frame_callback, &frame_listener, state);

    wl_surface_commit(state->surface);

    buffer->busy = 1;

    uint32_t d = time - state->last_frame_time;
    if (d > 0)
        state->fps = 1000 / d;

    HOG("fps: %d", state->fps);

    state->last_frame_time = time;
    state->frame_count++;
}

void
set_signal_handlers()
{
    struct sigaction dfl_action;
    dfl_action.sa_handler = SIG_DFL;
    sigemptyset(&dfl_action.sa_mask);
    dfl_action.sa_flags = SA_RESETHAND;

    for (int i = 1; i < SIGRTMAX; i++)
        sigaction(i, &dfl_action, NULL);
}

int
main(int argc, char* argv[])
{

    struct state* state;
    state = malloc(sizeof(*state));
    state->width = 700;
    state->height = 600;
    state->last_frame_time = 0;
    state->frame_count = 0;
    state->keep_running = 1;
    state->buff1 = NULL;
    state->buff2 = NULL;

    state->display = wl_display_connect(NULL);
    if (!state->display) {
        HOG_ERR("Failed to connect to Wayland display.");
        return 1;
    }
    HOG_INFO("Connection established!");

    state->registry = wl_display_get_registry(state->display);
    wl_registry_add_listener(state->registry, &registry_listener, state);

    wl_display_roundtrip(state->display);
    if (state->shm == NULL) {
        HOG_ERR("No wl_shm global");
        return 1;
    }

    state->surface = wl_compositor_create_surface(state->compositor);

    setup_xdg_shell(state);

    wl_surface_commit(state->surface);

    while (wl_display_dispatch(state->display) != -1 && state->keep_running) {
    }

    // TODO free all
    wl_display_disconnect(state->display);
    free(state);
    return 0;
}
