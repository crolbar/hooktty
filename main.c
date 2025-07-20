#include <assert.h>
#include <dll.h>
#include <fcntl.h>
#include <ft2build.h>
#include <locale.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <pixman.h>
#include <uchar.h>

#include <fcntl.h>
#include <pthread.h>
#include <pty.h>

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

struct font*
find_fallback_font(struct state* state, uint32_t ch)
{
    dll_for_each(state->fallback_fonts, i)
    {
        if (!FcCharSetHasChar(i->val.fc_charset, ch))
            continue;
        return &i->val;
    }
    return NULL;
}

char*
keep_last_n_lines(char* str, int n)
{
    if (n <= 0 || !str)
        return "";

    int len = strlen(str);
    int count = 0;

    // Traverse from the end to the start
    for (int i = len - 1; i >= 0; i--) {
        if (str[i] == '\n') {
            count++;
            if (count == n + 1) {
                // We found the (n+1)th newline — truncate string here
                return &str[i + 1];
            }
        }
    }

    return str;
}

static void
paint_data(struct state* state, struct buffer* buff, uint32_t time)
{
    int height = state->height * state->output_scale_factor;
    int width = state->width * state->output_scale_factor;

    uint32_t* data =
      state->shm_data +
      buff->offset / 4; // buff->offset is byte offset, here we don't need bytes

    pixman_image_t* buf_img = pixman_image_create_bits_no_clear(
      PIXMAN_a8r8g8b8, width, height, data, width * 4);

    pixman_region32_t clip;
    pixman_region32_init_rect(&clip, 0, 0, width, height);
    pixman_image_set_clip_region32(buf_img, &clip);
    pixman_region32_fini(&clip);

    /* Background */
    pixman_image_fill_rectangles(
      PIXMAN_OP_SRC,
      buf_img,
      &bg,
      1,
      (pixman_rectangle16_t[]){ { 0, 0, width, height } });

    pixman_image_t* color_img = pixman_image_create_solid_fill(&fg);

    if (state->text_buf_size == 0)
        return;

    pthread_mutex_lock(&state->text_buf_mutex);

    const char* u_text = keep_last_n_lines(state->text_buf, state->rows - 1);

    char32_t* text = calloc(strlen(u_text) + 1, sizeof(char32_t));
    size_t text_len;
    mbstate_t ps = { 0 };

    {
        const char* in = u_text;
        const char* end = u_text + strlen(u_text) + 1;

        size_t ret;

        while ((ret = mbrtoc32(&text[text_len], in, end - in, &ps)) != 0) {
            in += ret;
            text_len++;
        }
    }

    pthread_mutex_unlock(&state->text_buf_mutex);

    FT_Error ft_err;
    double x_offset = 0;
    int y_offset = 0;

    struct font* font = &state->font;
    assert(font == &state->font);

    for (int i = 0; i < text_len; i++) {
        uint32_t ch = text[i];

        if (!FcCharSetHasChar(font->fc_charset, ch)) {
            struct font* fallback_font = find_fallback_font(state, ch);

            if (fallback_font == NULL)
                HOG_ERR(
                  "char: %c not found in fallback fonts nor in specified font",
                  ch);
            else
                font = fallback_font;
        }

        int font_height = font->ft_face->size->metrics.height / 63.;

        if (ch == '\n') {
            y_offset += font_height + 5;
            x_offset = 0;
            continue;
        }

        if (ch == '\r')
            continue;

        int glyph_index = FT_Get_Char_Index(font->ft_face, ch);

        ft_err = FT_Load_Glyph(font->ft_face, glyph_index, FT_LOAD_DEFAULT);
        if (ft_err != FT_Err_Ok) {
            HOG_ERR("Failed to load glyph");
            abort();
        }

        ft_err = FT_Render_Glyph(font->ft_face->glyph, FT_RENDER_MODE_NORMAL);
        if (ft_err != FT_Err_Ok) {
            HOG_ERR("Failed to load glyph");
            abort();
        }

        FT_Bitmap bitmap = font->ft_face->glyph->bitmap;

        int stride =
          (((PIXMAN_FORMAT_BPP(PIXMAN_a8) * bitmap.width + 7) / 8 + 4 - 1) &
           -4);

        uint8_t* glyph_pix = malloc(bitmap.rows * stride);
        if (stride == bitmap.pitch) {
            memcpy(glyph_pix, bitmap.buffer, bitmap.rows * stride);
        } else {
            for (size_t r = 0; r < bitmap.rows; r++) {
                for (size_t c = 0; c < bitmap.width; c++)
                    glyph_pix[r * stride + c] =
                      bitmap.buffer[r * bitmap.pitch + c];
            }
        }

        pixman_image_t* glyph_img = pixman_image_create_bits_no_clear(
          PIXMAN_a8, bitmap.width, bitmap.rows, (uint32_t*)glyph_pix, stride);

        int dst_x = x_offset + font->ft_face->glyph->bitmap_left;
        int dst_y = y_offset - font->ft_face->glyph->bitmap_top;

        pixman_image_composite(PIXMAN_OP_OVER,
                               color_img,
                               glyph_img,
                               buf_img,
                               0,
                               0,
                               0,
                               0,
                               dst_x,
                               dst_y,
                               bitmap.width,
                               bitmap.rows);

        pixman_image_unref(glyph_img);
        free(glyph_pix);

        x_offset += font->ft_face->glyph->advance.x / 63.;

        // set back to default font if using fallback
        if (font != &state->font)
            font = &state->font;
    }

    pixman_image_unref(color_img);
    pixman_image_unref(buf_img);
    free(text);
}

void
new_buffers(struct state* state)
{
    int height = state->height * state->output_scale_factor;
    int width = state->width * state->output_scale_factor;
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
update_buffs(struct state* state)
{
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
}

static void
update_grid(struct state* state)
{
    if (!state->font.ft_face)
        return;

    FT_Load_Char(state->font.ft_face, 'M', FT_LOAD_DEFAULT);
    int char_width = state->font.ft_face->glyph->advance.x >> 6;

    int char_height = state->font.ft_face->size->metrics.height >> 6;

    HOG("max: %d, w: %d", char_width, state->width);

    state->cols = state->width / char_width;
    state->rows = state->height / char_height;

    HOG("cols: %d, rows: %d", state->cols, state->rows);
}

void
redraw(struct state* state, uint32_t time)
{
    if (!state->needs_redraw)
        return;

    if (state->window_resized) {
        update_buffs(state);
        update_grid(state);

        state->window_resized = false;
    }

    struct buffer* buffer = get_free_buff(state);

    paint_data(state, buffer, time);

    wl_surface_attach(state->surface, buffer->buffer, 0, 0);
    wl_surface_damage(state->surface,
                      0,
                      0,
                      state->width * state->output_scale_factor,
                      state->height * state->output_scale_factor);

    wl_surface_set_buffer_scale(state->surface, state->output_scale_factor);

    buffer->busy = 1;

    uint32_t d = time - state->last_frame_time;
    if (d > 0)
        state->fps = 1000 / d;

    // HOG("fps: %d", state->fps);

    state->last_frame_time = time;
    state->frame_count++;

    state->needs_redraw = false;
}

void
frame_callback(void* data, struct wl_callback* callback, uint32_t time)
{
    struct state* state = data;

    wl_callback_destroy(callback);
    state->frame_callback = NULL;

    redraw(state, time);

    state->frame_callback = wl_surface_frame(state->surface);
    wl_callback_add_listener(state->frame_callback, &frame_listener, state);

    wl_surface_commit(state->surface);
}

static void
set_font_face_size(struct font font, FT_UInt pixel_size, int32_t scale)
{
    FT_Error ft_err =
      FT_Set_Char_Size(font.ft_face, (pixel_size * scale) * 64., 0, 96, 96);

    if (ft_err != FT_Err_Ok)
        HOG_ERR("Failed to char pixel size on ft_face to: %d on font %s",
                pixel_size,
                font.ttf);
}

static void
reset_ft_face_size(struct state* state)
{
    set_font_face_size(
      state->font, state->ft_pixel_size, state->output_scale_factor);

    dll_for_each(state->fallback_fonts, v)
    {
        set_font_face_size(
          v->val, state->ft_pixel_size, state->output_scale_factor);
    }
}

void
handle_wl_output_scale(void* data, struct wl_output* wl_output, int32_t factor)
{
    struct state* state = data;
    state->output_scale_factor = factor;
    reset_ft_face_size(state);

    // HOG("scale factor: %d", factor);
    // TODO: handle different wl_outputs

    state->window_resized = true;
}

void
handle_wl_output_geometry(void* data,
                          struct wl_output* wl_output,
                          int32_t x,
                          int32_t y,
                          int32_t physical_width,
                          int32_t physical_height,
                          int32_t subpixel,
                          const char* make,
                          const char* model,
                          int32_t transform)
{
}

void
handle_wl_output_mode(void* data,
                      struct wl_output* wl_output,
                      uint32_t flags,
                      int32_t width,
                      int32_t height,
                      int32_t refresh)
{
}

void
handle_wl_output_done(void* data, struct wl_output* wl_output)
{
}

static const struct wl_output_listener wl_output_listener = {
    .geometry = handle_wl_output_geometry,
    .mode = handle_wl_output_mode,
    .done = handle_wl_output_done,
    .scale = handle_wl_output_scale,
};

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
          wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base =
          wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &xdg_wm_base_listener, state);
    } else if (strcmp(interface, "wl_seat") == 0) {
        state->seat =
          wl_registry_bind(wl_registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "wl_output") == 0) {
        state->output =
          wl_registry_bind(wl_registry, name, &wl_output_interface, 2);
        wl_output_add_listener(state->output, &wl_output_listener, state);
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

struct font
init_font(FT_Library ft,
          FT_UInt ft_pixel_size,
          int32_t scale,
          FcPattern* pattern)
{
    FcChar8* ttf = NULL;
    FcPatternGetString(pattern, FC_FILE, 0, &ttf);
    assert(ttf != NULL);

    FT_Face ft_face;
    FcCharSet* fc_charset;
    if (FcPatternGetCharSet(pattern, FC_CHARSET, 0, &fc_charset) !=
        FcResultMatch) {
        HOG_ERR("failed to get charset");
        abort();
    }

    FT_Error ft_err = FT_New_Face(ft, (const char*)ttf, 0, &ft_face);
    if (ft_err != FT_Err_Ok) {
        HOG_ERR("Failed to open font file: %s", ttf);
        abort();
    }

    ft_err =
      FT_Set_Char_Size(ft_face, (ft_pixel_size * scale) * 64., 0, 96, 96);
    if (ft_err != FT_Err_Ok)
        HOG_ERR("Failed to char pixel size on ft_face to: %d on font %s",
                ft_pixel_size,
                ttf);

    return (
      struct font){ .ttf = ttf, .fc_charset = fc_charset, .ft_face = ft_face };
}

void
init_freetype(struct state* state)
{
    FT_Init_FreeType(&state->ft);

    FcPattern* pattern;
    FcPattern* matched;
    FcResult result;

    {
        pattern = FcNameParse((const FcChar8*)state->font_name);
        assert(pattern != NULL);
        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        matched = FcFontMatch(NULL, pattern, &result);
        assert(matched != NULL);
        assert(result == FcResultMatch);
    }

    state->fallback_fonts = (typeof(state->fallback_fonts))dll_init();

    FcFontSet* font_set = FcFontSort(NULL, pattern, FcTrue, NULL, &result);

    for (int i = 0; i < font_set->nfont; i++) {
        FcPattern* pattern = font_set->fonts[i];
        dll_push_tail(state->fallback_fonts,
                      init_font(state->ft,
                                state->ft_pixel_size,
                                state->output_scale_factor,
                                pattern));

        HOG("loaded font: %s", state->fallback_fonts.tail->val.ttf);
    }

    state->font = init_font(
      state->ft, state->ft_pixel_size, state->output_scale_factor, matched);
    HOG_INFO("loaded font: %s", state->font.ttf);
}

static char ANSI_ESC = '\x1b';

void
strip_ansi_codes(char* str)
{
    char *src = str, *dst = str;
    while (*src) {

        // encounter CSI
        if (*src == ANSI_ESC && src[1] && src[1] == '[') {

            src += 2; // skip esc + [

            while (*src && !((*src >= 'A' && *src <= 'Z') ||
                             (*src >= 'a' && *src <= 'z')))
                src++;

            // skip end char
            if (*src)
                src++;

            continue;
        }

        // encounter OSC
        else if (*src == ANSI_ESC && src[1] && src[1] == ']') {

            src += 2;

            // skip untill ST (0x1B 0x5C) or BEL (0x07)
            while (*src &&
                   !((*src == ANSI_ESC && src[1] == '\\') || (*src == '\x07')))
                src++;

            // skip ST/BEL
            if (*src == '\x07') {
                src++;
            } else if (*src == ANSI_ESC && src[1] == '\\') {
                src += 2;
            }

            continue;
        }

        // copy normal chars over
        *dst++ = *src++;
    }

    *dst = '\0';
}

void*
pty_reader_thread(void* data)
{
    struct state* state = data;

    char buf[1024 * 4];

    int log_fd = open("log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    while (state->keep_running) {
        ssize_t n = read(state->master_fd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            HOG_ERR("pty read error");
            break;
        }
        buf[n] = '\0';

        strip_ansi_codes(buf);

        HOG("buf n: %d, strlen: %d", n, strlen(buf));

        n = strlen(buf);
        if (n == 0)
            continue;

        pthread_mutex_lock(&state->text_buf_mutex);

        int old_size = state->text_buf_size;
        state->text_buf_size += n;

        // init the buff
        if (state->text_buf_size == n)
            state->text_buf = malloc(state->text_buf_size + 1);
        else {
            char* new = realloc(state->text_buf, state->text_buf_size + 1);

            if (!new) {
                HOG_ERR("could not realloc new text buf with size: %d",
                        state->text_buf_size);
                free(state->text_buf);
                return NULL;
            }

            state->text_buf = new;
        }

        memcpy(state->text_buf + old_size, buf, n);
        state->text_buf[state->text_buf_size] = '\0';

        state->needs_redraw = true;

        pthread_mutex_unlock(&state->text_buf_mutex);

        write(log_fd, buf, n);
    }
    close(log_fd);

    return NULL;
}

void
start_pty(struct state* state)
{
    pid_t pid;
    struct winsize ws = {
        .ws_row = 24, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0
    };

    pid = forkpty(&state->master_fd, NULL, NULL, &ws);
    if (pid == -1) {
        perror("forkpty");
        exit(1);
    }

    if (pid == 0) {
        execl("/run/current-system/sw/bin/bash", "bash", NULL);
        perror("execl");
        exit(1);
    } else {
        pthread_t tid;
        pthread_create(&tid, NULL, pty_reader_thread, state);
    }
}

int
main(int argc, char* argv[])
{
    set_signal_handlers();

    setlocale(LC_ALL, "C.UTF-8");

    struct state* state;
    state = malloc(sizeof(*state));
    state->width = 350;
    state->height = 300;
    state->last_frame_time = 0;
    state->frame_count = 0;
    state->keep_running = 1;
    state->buff1 = NULL;
    state->buff2 = NULL;
    state->ft_pixel_size = 10;
    state->output_scale_factor = 1;
    state->font_name = "Hack";
    state->text_buf_size = 0;
    state->text_buf = "";
    state->text_buf_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    state->needs_redraw = true;

    // const char* text =
    //   "hello world 󰣨   | ligatures: fi | اَلْعَرَبِيَّةُ
    //   ";
    // state->text_buf = malloc(3000);
    // strcpy(state->text_buf, text);

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

    start_pty(state);

    while (wl_display_dispatch(state->display) != -1 && state->keep_running) {
    }

    // TODO free all
    wl_display_disconnect(state->display);
    free(state);
    return 0;
}
