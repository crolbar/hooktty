#include "string.h"
#include "wayland-client-protocol.h"
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

#define NEW_LINE_GLYPH U'⏎'

#define ANSI_ESC '\x1b'

#define ANSI_MAX_NUM_PARAMS 16
#define ANSI_FINAL_SGR 'm'

static const float alpha = 0.8;
static const struct color COLOR_BACKGROUND = { 0,
                                               0,
                                               0,
                                               (unsigned char)(255 * alpha) };
static const struct color COLOR_FOREGROUND = { 255, 255, 255, 255 };
static const struct attributes DEFAULT_ATTRS = {
    COLOR_FOREGROUND,
    COLOR_BACKGROUND,
};

static const struct color COLOR_BRIGHT_0 = { 57, 57, 57, 255 };
static const struct color COLOR_BRIGHT_1 = { 238, 83, 150, 255 };
static const struct color COLOR_BRIGHT_2 = { 66, 190, 101, 255 };
static const struct color COLOR_BRIGHT_3 = { 255, 233, 123, 255 };
static const struct color COLOR_BRIGHT_4 = { 51, 177, 255, 255 };
static const struct color COLOR_BRIGHT_5 = { 255, 126, 182, 255 };
static const struct color COLOR_BRIGHT_6 = { 61, 219, 217, 255 };
static const struct color COLOR_BRIGHT_7 = { 255, 255, 255, 255 };

static const struct color COLOR_REGULAR_0 = { 0, 0, 0, 255 };
static const struct color COLOR_REGULAR_1 = { 255, 126, 182, 255 };
static const struct color COLOR_REGULAR_2 = { 66, 190, 101, 255 };
static const struct color COLOR_REGULAR_3 = { 255, 233, 123, 255 };
static const struct color COLOR_REGULAR_4 = { 51, 177, 255, 255 };
static const struct color COLOR_REGULAR_5 = { 255, 126, 182, 255 };
static const struct color COLOR_REGULAR_6 = { 61, 219, 217, 255 };
static const struct color COLOR_REGULAR_7 = { 221, 225, 230, 255 };

static const struct color SYSTEM_COLORS[16] = {
    COLOR_REGULAR_0, COLOR_REGULAR_1, COLOR_REGULAR_2, COLOR_REGULAR_3,
    COLOR_REGULAR_4, COLOR_REGULAR_5, COLOR_REGULAR_6, COLOR_REGULAR_7,

    COLOR_BRIGHT_0,  COLOR_BRIGHT_1,  COLOR_BRIGHT_2,  COLOR_BRIGHT_3,
    COLOR_BRIGHT_4,  COLOR_BRIGHT_5,  COLOR_BRIGHT_6,  COLOR_BRIGHT_7,
};

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

static struct pixman_color
color_to_pixman_color(struct color color)
{
    return (struct pixman_color){
        (uint16_t)(color.r * 257),
        (uint16_t)(color.g * 257),
        (uint16_t)(color.b * 257),
        (uint16_t)(color.a * 257),
    };
}

static void
render_char_at(struct font* font,
               pixman_image_t* buf_img,
               struct cell* cell,
               int x,
               int y,
               int cell_height,
               int cell_width)
{
    FT_Error ft_err;

    int font_height = font->ft_face->size->metrics.height / 63.;

    int glyph_index = FT_Get_Char_Index(font->ft_face, cell->ch);

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
      (((PIXMAN_FORMAT_BPP(PIXMAN_a8) * bitmap.width + 7) / 8 + 4 - 1) & -4);

    uint8_t* glyph_pix = malloc(bitmap.rows * stride);
    if (stride == bitmap.pitch) {
        memcpy(glyph_pix, bitmap.buffer, bitmap.rows * stride);
    } else {
        for (size_t r = 0; r < bitmap.rows; r++) {
            for (size_t c = 0; c < bitmap.width; c++)
                glyph_pix[r * stride + c] = bitmap.buffer[r * bitmap.pitch + c];
        }
    }

    pixman_image_t* glyph_img = pixman_image_create_bits_no_clear(
      PIXMAN_a8, bitmap.width, bitmap.rows, (uint32_t*)glyph_pix, stride);

    int dst_x = x + (cell_width - bitmap.width) / 2;

    int ascent = font->ft_face->size->metrics.ascender / 63.;
    int baseline = y - cell_height + ascent;
    int dst_y = baseline - font->ft_face->glyph->bitmap_top;

    struct pixman_color fg = color_to_pixman_color(cell->attrs.fg);
    struct pixman_color bg = color_to_pixman_color(cell->attrs.bg);

    pixman_image_t* color_img = pixman_image_create_solid_fill(&fg);

    pixman_image_fill_rectangles(
      PIXMAN_OP_SRC,
      buf_img,
      &bg,
      1,
      (pixman_rectangle16_t[]){
        { x, y - cell_height, cell_width, cell_height } });

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
    pixman_image_unref(color_img);
    free(glyph_pix);
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

    struct pixman_color bg = color_to_pixman_color(COLOR_BACKGROUND);
    pixman_image_fill_rectangles(
      PIXMAN_OP_SRC,
      buf_img,
      &bg,
      1,
      (pixman_rectangle16_t[]){ { 0, 0, width, height } });

    struct font* font = &state->font;
    assert(font == &state->font);

    FT_Load_Char(state->font.ft_face, 'M', FT_LOAD_DEFAULT);
    int x_adv = font->ft_face->glyph->advance.x / 63.;
    int y_adv = font->ft_face->size->metrics.height / 63.;

    int row_num = state->rows; // TODO

    assert(state->grid != NULL);
    if (state->grid[0] == NULL)
        return;

    // TODO: use front/back grids
    pthread_mutex_lock(&state->grid_mutex);

    for (int row_idx = 0; row_idx < row_num; row_idx++) {
        // skip unused rows
        if (state->grid[row_idx]->cells[0].ch == 0) {
            break;
        }

        struct row* row = state->grid[row_idx];

        for (int col_idx = 0; col_idx < row->len; col_idx++) {
            // Don't render chars after the cursor pos (probably 0's)
            if (col_idx == state->cursor.x && row_idx == state->cursor.y)
                break;

            struct cell* cell = &row->cells[col_idx];

            uint32_t ch = cell->ch;

            if (!FcCharSetHasChar(font->fc_charset, ch)) {
                struct font* fallback_font = find_fallback_font(state, ch);

                if (fallback_font == NULL)
                    HOG_ERR("char: %c not found in fallback fonts nor in "
                            "specified font",
                            ch);
                else
                    font = fallback_font;
            }

            render_char_at(font,
                           buf_img,
                           cell,
                           (col_idx + 1) * x_adv,
                           (row_idx + 1) * y_adv,
                           state->cell_height,
                           state->cell_width);

            // set back to default font if using fallback
            if (font != &state->font)
                font = &state->font;

            // move to the next row
            if (ch == NEW_LINE_GLYPH)
                break;
        }
    }

    struct cell cursor = { U'\u2588',
                           { { 255, 120, 180, 255 }, COLOR_BACKGROUND } };
    render_char_at(&state->font,
                   buf_img,
                   &cursor,
                   (state->cursor.x + 1) * x_adv,
                   (state->cursor.y + 1) * y_adv,
                   state->cell_height,
                   state->cell_width);

    pthread_mutex_unlock(&state->grid_mutex);

    pixman_image_unref(buf_img);
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

static struct row*
init_row(uint16_t size)
{
    struct row* row = malloc(sizeof(struct row));
    row->cells = calloc(size, sizeof(*row->cells));

    struct attributes attrs = DEFAULT_ATTRS;

    for (int i = 0; i < size; i++) {
        row->cells[i].ch = 0;
        row->cells[i].attrs = attrs;
    }

    row->len = size;

    return row;
}

static void
update_grid(struct state* state)
{
    if (!state->font.ft_face)
        return;

    FT_Load_Char(state->font.ft_face, 'M', FT_LOAD_DEFAULT);
    int char_width = state->font.ft_face->glyph->advance.x >> 6;
    int char_height = state->font.ft_face->size->metrics.height >> 6;

    uint16_t cols = (state->width * state->output_scale_factor) / char_width;
    uint16_t rows = (state->height * state->output_scale_factor) / char_height;

    bool is_bigger = state->rows < rows;

    uint16_t old_rows = state->rows;

    state->cols = cols;
    state->rows = rows;
    state->cell_height = char_height;
    state->cell_width = char_width;

    HOG("cols: %d, rows: %d", state->cols, state->rows);

    if (old_rows == rows)
        return;

    // init grid
    if (state->grid == NULL) {
        state->grid = malloc(state->rows * sizeof(struct row*));

        for (int i = 0; i < rows; i++) {
            state->grid[i] = init_row(state->cols);
        }
        return;
    }

    if (is_bigger) {
        struct row** new = realloc(state->grid, rows * sizeof(struct row*));

        assert(new != NULL);

        state->grid = new;

        for (int i = old_rows; i < rows; i++) {
            state->grid[i] = init_row(state->cols);
        }
    }

    if (!is_bigger) {
        assert(!"TODO");
    }
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

static void
pop_first_grid_row(struct state* state)
{
    struct row* f = state->grid[0];
    for (int i = 0; i < f->len; i++)
        f->cells[i].ch = 0;

    int i = 1;
    for (size_t i = 1; i < state->rows; ++i) {
        if (state->grid[i] == NULL)
            break;
        state->grid[i - 1] = state->grid[i];
    }

    state->grid[state->rows - 1] = f;
}

static int
get_ansi_params_len(int* params)
{
    int i = 0;
    while (params[i] != -1 && i < ANSI_MAX_NUM_PARAMS)
        i++;
    return i;
}

static const char*
parse_ansi_params(const char* s, int* params)
{
    if (!((*s >= '0' && *s <= '9') || *s == ';' || *s == '?')) {
        HOG("input sequence ?: %s", s);
    }

    int num = 0;
    int i = 0;
    while (!(*s >= '@' && *s <= '~')) {
        if (!*s) {
            return NULL;
        }

        if (*s == ';') {
            params[i++] = num;
            num = 0;

            s++;
            continue;
        }

        if (*s == '?') {
            params[i++] = '?';

            s++;
            continue;
        }

        num *= 10;
        num += *s - '0';

        s++;
    }
    params[i++] = num;

    params[i++] = -1;

    return s;
}

static const struct color
parse_ansi_color(int* params, int* i)
{
    switch (params[*i]) {
        case 5:
            (*i)++;
            HOG("256 %d color", params[*i]);

            // 0-15
            if (params[*i] < 16) {
                return SYSTEM_COLORS[params[*i]];
            }

            // 16-231
            if (params[*i] < 232) {
                uint8_t idx = params[*i] - 16;

                uint8_t red_idx = idx / 36;
                uint8_t green_idx = (idx % 36) / 6;
                uint8_t blue_idx = idx % 6;

                uint8_t red = 0, green = 0, blue = 0;
                if (red_idx != 0)
                    red = red_idx * 40 + 55;
                if (green_idx != 0)
                    green = green_idx * 40 + 55;
                if (blue_idx != 0)
                    blue = blue_idx * 40 + 55;

                return (struct color){ red, green, blue, 255 };
            }

            // 232-255
            uint8_t code = params[*i];
            uint8_t g = (code - 232) * 10 + 8;
            return (struct color){ g, g, g, 255 };

            (*i)++;

            break;
        case 2: // TODO
            (*i) += 3 + 1;
            break;
    }

    return COLOR_FOREGROUND;
}

static const char*
parse_ansi_csi(struct state* state, const char* s, struct attributes* attrs)
{
    const char* in = s;

    // skip [
    s++;
    if (!*s)
        return NULL;

    int params[ANSI_MAX_NUM_PARAMS] = { 0 };

    const char* _s = parse_ansi_params(s, params);
    if (!_s) {
        return NULL;
    }
    s = _s;

    assert((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'));

    // match final byte
    switch (*s) {
        case ANSI_FINAL_SGR: {

            int len = get_ansi_params_len(params);

            HOG("SGR: %d", len);

            for (int i = 0; i < len; i++) {
                switch (params[i]) {
                    case 0:
                        *attrs = (struct attributes)DEFAULT_ATTRS;
                        break;

                    // FOREGROUND COLORS
                    case 30:
                    case 31:
                    case 32:
                    case 33:
                    case 34:
                    case 35:
                    case 36:
                    case 37:
                        attrs->fg = SYSTEM_COLORS[params[i] - 30];
                        break;
                    case 38:
                        i++;
                        attrs->fg = parse_ansi_color(params, &i);
                        break;
                    case 39:
                        attrs->fg = COLOR_FOREGROUND;
                        break;
                    case 90:
                    case 91:
                    case 92:
                    case 93:
                    case 94:
                    case 95:
                    case 96:
                    case 97:
                        attrs->fg = SYSTEM_COLORS[(params[i] - 90) + 8];
                        break;

                    // BACKGROUND COLORS
                    case 40:
                    case 41:
                    case 42:
                    case 43:
                    case 44:
                    case 45:
                    case 46:
                    case 47:
                        attrs->bg = SYSTEM_COLORS[params[i] - 40];
                        break;
                    case 48:
                        i++;
                        attrs->bg = parse_ansi_color(params, &i);
                        break;
                    case 49:
                        attrs->bg = COLOR_BACKGROUND;
                        break;
                    case 100:
                    case 101:
                    case 102:
                    case 103:
                    case 104:
                    case 105:
                    case 106:
                    case 107:
                        attrs->bg = SYSTEM_COLORS[(params[i] - 100) + 8];
                        break;
                }
            }

            break;
        }
    }

    if (*s)
        s++;

    {
        char buf[1024] = { 0 };
        memcpy(buf, in, s - in);
        HOG("CSI: %s", buf);
        for (int i = 0; i < ANSI_MAX_NUM_PARAMS && params[i] != -1; i++) {
            HOG("p: %d", params[i]);
        }
        HOG("");
    }

    return s;
}

static const char*
parse_ansi_osc(struct state* state, const char* s, struct attributes* attrs)
{
    // skip ]
    s++;

    if (!*s) {
        return NULL;
    }
    while (!((*s == ANSI_ESC && s[1] == '\\') || (*s == '\x07'))) {
        if (!*s) {
            return NULL;
        }

        s++;
    }

    if (*s)
        s++;
    return s;
}

// returning pointer to the first char after the ansi code
static const char*
parse_ansi(struct state* state, const char* s, struct attributes* attrs)
{
    assert(*(s - 1) == ANSI_ESC);

    switch (*s) {
        case '[':
            return parse_ansi_csi(state, s, attrs);
            break;
        case ']':
            return parse_ansi_osc(state, s, attrs);
            break;
        // TODO: handle other multichar sequences ?
        default:
            s++;
            break;
    }

    // HOG("returing: %s", s);

    return s;
}

static const char*
parse_pty_output(struct state* state, char* buf, int n)
{

    assert(state->grid != NULL);
    assert(state->cursor.x < state->cols);
    assert(state->cursor.y < state->rows);

    point* cur = &state->cursor;
    const char* s = buf;

    const char* end = s + n + 1;

    struct attributes attrs = DEFAULT_ATTRS;
    while (*s) {
        assert(cur->y < state->rows);
        assert(cur->x < state->cols);

        char32_t ch = 0;
        {
            mbstate_t p = { 0 };
            size_t size = mbrtoc32(&ch, s, end - s, &p);
            if (size < 0) {
                HOG_ERR("mbrtoc size < 1: %d, char: %c(%d)", size, ch, ch);
                abort();
            }
            s += size;
        }

        // HOG("set: %c(%d) at %d,%d", ch, ch, cur.y, cur.x);

        /* reusing an row after the screen has been resized */
        if (state->grid[cur->y]->len != state->cols) {
            if (state->grid[cur->y] != NULL)
                free(state->grid[cur->y]);

            state->grid[cur->y] = init_row(state->cols);
        }

        // ansi parser
        if (ch == ANSI_ESC) {
            const char* _s = parse_ansi(state, s, &attrs);

            if (!_s) {
                return s - 1;
            }

            s = _s;

            continue;
        }

        /* go to col 0 */
        if (ch == U'\r') {
            cur->x = 0;
            continue;
        }

        /* move down a row */
        if (ch == U'\n') {
            for (int i = 0; i < state->grid[cur->y]->len; i++) {
                if (state->grid[cur->y]->cells[i].ch == 0) {

                    state->grid[cur->y]->cells[i].ch = NEW_LINE_GLYPH;

                    state->grid[cur->y]->cells[i].attrs.fg =
                      (struct color){ 255, 155, 255, 255 };
                    state->grid[cur->y]->cells[i].attrs.bg = COLOR_BACKGROUND;

                    break;
                }
            }

            cur->y++;

            if (cur->y >= state->rows) {
                cur->y = state->rows - 1;
                pop_first_grid_row(state);
            }
            continue;
        }

        {
            state->grid[cur->y]->cells[cur->x].ch = ch;
            state->grid[cur->y]->cells[cur->x].attrs = attrs;

            cur->x++;
        }

        /* line wrap */
        if (cur->x >= state->cols) {
            cur->y++;
            cur->x = 0;

            if (cur->y >= state->rows) {
                cur->y = state->rows - 1;
                pop_first_grid_row(state);
            }
        }
    }

    return NULL;
}

#define PENDING_BUF_SIZE 8192

void*
pty_reader_thread(void* data)
{
    struct state* state = data;

    int log_fd = open("log", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    char buf[1024 * 4];

    char pending_buf[PENDING_BUF_SIZE];
    size_t pending_buf_len = 0;

    while (state->keep_running) {
        ssize_t n = read(state->master_fd, buf, sizeof(buf));

        if (n <= 0) {
            HOG_ERR("pty read error");
            break;
        }

        // HOG("buf n: %d, strlen: %d", n, strlen(buf));

        if (pending_buf_len + n > PENDING_BUF_SIZE) {
            HOG_ERR("pending buffer overflowed");
            pending_buf_len = 0;
            continue;
        }

        memcpy(pending_buf + pending_buf_len, buf, n);
        pending_buf[pending_buf_len + n] = '\0';
        pending_buf_len += n;

        pthread_mutex_lock(&state->grid_mutex);

        const char* left_over =
          parse_pty_output(state, pending_buf, pending_buf_len);

        state->needs_redraw = true;

        pthread_mutex_unlock(&state->grid_mutex);

        // if we have non parsed part (ex: cut offed ansi code)
        if (left_over != NULL) {
            // NOTE: other cases that ansi cut off ?
            assert(*left_over == ANSI_ESC);

            size_t left_over_size = strlen(left_over);

            memmove(pending_buf,
                    pending_buf + (pending_buf_len - left_over_size),
                    left_over_size);
            pending_buf_len = left_over_size;
        } else {
            pending_buf_len = 0;
        }

        write(log_fd, buf, n);
        char* sep = "\n=========\n";
        write(log_fd, sep, strlen(sep));
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
    state->needs_redraw = true;
    state->grid = NULL;
    state->grid_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    state->cursor = (point){ 0, 0 };

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
