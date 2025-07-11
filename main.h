#pragma once

#include <fontconfig/fontconfig.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <dll.h>

struct font
{
    FT_Face ft_face;
    FcCharSet* fc_charset;
    FcChar8* ttf;
};

struct state
{
    struct wl_display* display;
    struct wl_registry* registry;

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

    int keep_running;

    int need_update_buffs;

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
redraw(void* data, struct wl_callback* callback, uint32_t time);

static const struct wl_callback_listener frame_listener = { redraw };
