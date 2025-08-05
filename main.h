#pragma once

#include <fontconfig/fontconfig.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <dll.h>
#include <pthread.h>
#include <uchar.h>

#define HOOKTTY_LOGFILE
// #define HOOKTTY_LOGCSI

struct font
{
    FT_Face ft_face;
    FcCharSet* fc_charset;
    FcChar8* ttf;
};

struct color
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

struct attributes
{
    struct color fg;
    struct color bg;
    bool underline;
    bool inverse;
    bool bold;
};

struct cell
{
    char32_t ch;
    struct attributes attrs;
};

struct row
{
    struct cell* cells;
    // we are not resizing the grid
    // so not all rows will be of `cols` len
    size_t len;
};

typedef struct point
{
    uint16_t x;
    uint16_t y;
} point;

typedef struct cursor
{
    point p;
    bool lcf; // https://github.com/mattiase/wraptest
} cursor;

struct state
{
    struct row** grid; // size == rows
    struct row** alt_grid;
    pthread_mutex_t grid_mutex;

    struct winsize* ws;

    bool alt_screen;

    uint16_t top_margin;
    uint16_t btm_margin;

    uint16_t rows;
    uint16_t cols;
    int cell_width;
    int cell_height;

    struct cursor cursor;
    struct cursor alt_cursor;

    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_output* output;
    int32_t output_scale_factor;

    struct wl_compositor* compositor;
    struct xdg_wm_base* wm_base;
    struct wl_shm* shm;
    struct wl_seat* seat;

    struct wl_pointer* pointer;
    struct wl_keyboard* keyboard;

    struct wl_surface* surface;
    struct xdg_surface* xdg_surface;
    struct xdg_toplevel* xdg_toplevel;

    struct buffer* buff1;
    struct buffer* buff2;

    struct wl_callback* frame_callback;

    int32_t width, height;

    size_t size;
    uint32_t* shm_data;

    uint32_t last_frame_time;
    uint32_t frame_count;
    uint32_t fps;

    bool keep_running;

    bool window_resized;

    struct xkb_context* xkb_ctx;
    struct xkb_keymap* xkb_map;
    struct xkb_state* xkb_state;

    struct
    {
        xkb_mod_index_t mod_shift;
        xkb_mod_index_t mod_alt;
        xkb_mod_index_t mod_ctrl;
        xkb_mod_index_t mod_super;

        int shift;
        int alt;
        int ctrl;
        int super;
    } kbd;

    struct
    {
        wl_fixed_t x;
        wl_fixed_t y;
    } ptr;

    const char* font_name;
    FT_Library ft;
    FT_UInt ft_pixel_size;

    struct font font;
    dll(struct font) fallback_fonts;

    int master_fd;
    bool needs_redraw;

    struct
    {
        struct attributes attrs;
    } parser;
};

struct buffer
{
    struct wl_buffer* buffer;
    int busy;
    int offset;
};

void
new_buffers(struct state* state);

void
update_grid(struct state* state);

void
frame_callback(void* data, struct wl_callback* callback, uint32_t time);

static const struct wl_callback_listener frame_listener = { frame_callback };
