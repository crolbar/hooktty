#include <fcntl.h>
#include <ft2build.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include FT_FREETYPE_H
#include <pixman.h>
#include <uchar.h>

#include "macros.h"
#include "main.h"
#include "seat.h"
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

static pixman_color_t bg = { 0x0000, 0x0000, 0x0000, 0xffff };
static pixman_color_t fg = { 0xffff, 0xffff, 0xffff, 0xffff };

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

    pixman_image_t* pix = pixman_image_create_bits_no_clear(
      PIXMAN_a8r8g8b8, width, height, data, width * 4);

    pixman_region32_t clip;
    pixman_region32_init_rect(&clip, 0, 0, width, height);
    pixman_image_set_clip_region32(pix, &clip);
    pixman_region32_fini(&clip);

    /* Background */
    pixman_image_fill_rectangles(
      PIXMAN_OP_SRC,
      pix,
      &bg,
      1,
      (pixman_rectangle16_t[]){ { 0, 0, width, height } });

    pixman_image_t* color = pixman_image_create_solid_fill(&fg);

    // const char* text = "oeu󰗀 󰣨";
    const char* text = "Hello, World!  [0]  1 dots  󰗀 󰣨|";
    //char32_t* text;
    //size_t text_len;

    //{
    //    text = calloc(strlen(u_text) + 1, sizeof(text[0]));
    //    {
    //        mbstate_t ps = { 0 };
    //        const char* in = u_text;
    //        const char* const end = u_text + strlen(u_text) + 1;
    //
    //        size_t ret;
    //
    //        while ((ret = mbrtoc32(&text[text_len], in, end - in, &ps)) != 0) {
    //            switch (ret) {
    //                case (size_t)-1:
    //                    break;
    //
    //                case (size_t)-2:
    //                    break;
    //
    //                case (size_t)-3:
    //                    break;
    //            }
    //
    //            in += ret;
    //            text_len++;
    //        }
    //    }
    //}
    //size_t text_size = text_len;
    size_t text_size = strlen(text);

    double x_offset = 0;
    int baseline_y = 70;
    for (int i = 0; i < text_size; i++) {
        char ch = text[i];
        int glyph_index = FT_Get_Char_Index(state->ft_face, ch);

        FT_Error ft_err;

        ft_err = FT_Load_Glyph(state->ft_face, glyph_index, FT_LOAD_DEFAULT);
        if (ft_err != FT_Err_Ok) {
            HOG_ERR("Failed to load glyph");
            abort();
        }

        ft_err = FT_Render_Glyph(state->ft_face->glyph, FT_RENDER_MODE_NORMAL);
        if (ft_err != FT_Err_Ok) {
            HOG_ERR("Failed to load glyph");
            abort();
        }

        FT_Bitmap bitmap = state->ft_face->glyph->bitmap;
        HOG("pixel mode: %d", bitmap.pixel_mode);

        int stride =
          (((PIXMAN_FORMAT_BPP(PIXMAN_a8) * bitmap.width + 7) / 8 + 4 - 1) &
           -4);
        HOG("stride: %d, pitch: %d", stride, bitmap.pitch);

        uint8_t* glyph_pix = NULL;

        glyph_pix = malloc(bitmap.rows * stride);
        if (stride == bitmap.pitch) {
            memcpy(glyph_pix, bitmap.buffer, bitmap.rows * stride);
        } else {
            for (size_t r = 0; r < bitmap.rows; r++) {
                for (size_t c = 0; c < bitmap.width; c++)
                    glyph_pix[r * stride + c] =
                      bitmap.buffer[r * bitmap.pitch + c];
            }
        }

        pixman_image_t* mask = mask = pixman_image_create_bits_no_clear(
          PIXMAN_a8, bitmap.width, bitmap.rows, (uint32_t*)glyph_pix, stride);

        int dst_x = x_offset + state->ft_face->glyph->bitmap_left;
        int dst_y = baseline_y - state->ft_face->glyph->bitmap_top;

        pixman_image_composite(PIXMAN_OP_OVER,
                               color,
                               mask,
                               pix,
                               0,
                               0, // src origin
                               0,
                               0, // mask origin
                               dst_x,
                               dst_y, // dest position
                               bitmap.width,
                               bitmap.rows);

        pixman_image_unref(mask);
        free(glyph_pix);

        x_offset += state->ft_face->glyph->advance.x / 63.;
    }

    pixman_image_unref(color);
    pixman_image_unref(pix);
    //free(text);
}

void
new_buffers(struct state* state)
{
    int height = state->height;
    int width = state->width;
    int stride, size;

    stride = width * 4;
    size = stride * height * 2;
    state->size = size;

    char name[64];
    snprintf(name, sizeof(name), "/wl_hooktty_shm_buffer_pool-%d", getpid());
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    shm_unlink(name);

    if (fd < 0) {
        HOG_ERR("Creating a buffer file for %d B failed", size);
        abort();
    }

    if (ftruncate(fd, size) == -1) {
        HOG_ERR("Setting size of buffer file to: %d failed", size);
        close(fd);
        abort();
    }

    state->shm_data =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (state->shm_data == MAP_FAILED) {
        HOG_ERR("Mapping shm_data with mmap to fd failed");
        close(fd);
        abort();
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

    // HOG("fps: %d", state->fps);

    state->last_frame_time = time;
    state->frame_count++;
}

static void
registry_global(void* data,
                struct wl_registry* wl_registry,
                uint32_t name,
                const char* interface,
                uint32_t version)
{
    struct state* state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor =
          wl_registry_bind(wl_registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base =
          wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &xdg_wm_base_listener, state);
    } else if (strcmp(interface, "wl_seat") == 0) {
        state->seat =
          wl_registry_bind(wl_registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
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

void
init_freetype(struct state* state)
{
    FT_Init_FreeType(&state->ft);

    const char* ttf = getenv("FONT");
    //"/nix/store/8ajv1lva8bcdrcgqz50i6a72sq12i7sq-hack-font-3.003/share/fonts/"
    //"truetype/Hack-Regular.ttf";
    //"/nix/store/"
    //"gnkw1x0ni04mdrrwdcx42sdfrr69dv6k-nerd-fonts-symbols-only-3.4.0/share/"
    //"fonts/truetype/NerdFonts/Symbols/SymbolsNerdFont-Regular.ttf";

    FT_Error ft_err = FT_New_Face(state->ft, ttf, 0, &state->ft_face);
    if (ft_err != FT_Err_Ok) {
        HOG_ERR("Failed to open font file: %s", ttf);
        abort();
    }

    // HOG("size: %d", state->ft_face->size);

    ft_err = FT_Set_Pixel_Sizes(state->ft_face, 0, state->ft_pixel_size);
    if (ft_err != FT_Err_Ok) {
        HOG_ERR("Failed to set pixel sizes on ft_face to: %d",
                state->ft_pixel_size);
        abort();
    }
}

int
main(int argc, char* argv[])
{
    set_signal_handlers();

    struct state* state;
    state = malloc(sizeof(*state));
    state->width = 700;
    state->height = 600;
    state->last_frame_time = 0;
    state->frame_count = 0;
    state->keep_running = 1;
    state->buff1 = NULL;
    state->buff2 = NULL;
    state->ft_pixel_size = 52;

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

    init_seat_devs(state);

    state->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    init_freetype(state);

    while (wl_display_dispatch(state->display) != -1 && state->keep_running) {
    }

    // TODO free all
    wl_display_disconnect(state->display);
    free(state);
    return 0;
}
