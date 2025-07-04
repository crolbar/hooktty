#include <linux/input-event-codes.h>

#include "macros.h"
#include "main.h"
#include "seat.h"
#include "sys/mman.h"
#include "unistd.h"
#include "wayland-client-protocol.h"
#include "xkbcommon/xkbcommon.h"

void
handle_wl_pointer_motion(void* data,
                         struct wl_pointer* wl_pointer,
                         uint32_t time,
                         wl_fixed_t surface_x,
                         wl_fixed_t surface_y)
{
    struct state* state = data;
    state->ptr.x = surface_x;
    state->ptr.y = surface_y;

    // HOG("x: %d, y: %d", surface_x, surface_y);
}

void
handle_wl_pointer_button(void* data,
                         struct wl_pointer* wl_pointer,
                         uint32_t serial,
                         uint32_t time,
                         uint32_t button,
                         uint32_t state)
{
    struct state* s = data;

    if (button == BTN_LEFT) {
        if (state == WL_POINTER_BUTTON_STATE_RELEASED)
            HOG("letft btn released at (%d, %d)", s->ptr.x, s->ptr.y);
        if (state == WL_POINTER_BUTTON_STATE_PRESSED)
            HOG("letft btn pressed (%d, %d)", s->ptr.x, s->ptr.y);
    }
}

void
handle_wl_pointer_axis(void* data,
                       struct wl_pointer* wl_pointer,
                       uint32_t time,
                       uint32_t axis,
                       wl_fixed_t value)
{
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        HOG("val: %d", value);
    }
}

void
handle_wl_keyboard_enter(void* data,
                         struct wl_keyboard* wl_keyboard,
                         uint32_t serial,
                         struct wl_surface* surface,
                         struct wl_array* keys)
{
}

void
handle_wl_keyboard_leave(void* data,
                         struct wl_keyboard* wl_keyboard,
                         uint32_t serial,
                         struct wl_surface* surface)
{
}

void
handle_wl_keyboard_keymap(void* data,
                          struct wl_keyboard* wl_keyboard,
                          uint32_t format,
                          int32_t fd,
                          uint32_t size)
{
    struct state* state = data;

    if (state->xkb_map != NULL) {
        xkb_keymap_unref(state->xkb_map);
        state->xkb_map = NULL;
    }

    if (state->xkb_state != NULL) {
        xkb_state_unref(state->xkb_state);
        state->xkb_state = NULL;
    }

    if (format == WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP) {
        close(fd);
        HOG("no format");
        return;
    }

    char* map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (map_str == MAP_FAILED) {
        HOG_ERR("mmap on keyboard keymap failed");
        close(fd);
        return;
    }

    while (map_str[size - 1] == '\0')
        size--;

    state->xkb_map = xkb_keymap_new_from_buffer(state->xkb_ctx,
                                                map_str,
                                                size,
                                                XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);

    state->xkb_state = xkb_state_new(state->xkb_map);

    state->kbd.mod_shift =
      xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_SHIFT);
    state->kbd.mod_alt =
      xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_ALT);
    state->kbd.mod_ctrl =
      xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_CTRL);
    state->kbd.mod_super =
      xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_LOGO);

    munmap(map_str, size);
    close(fd);
}

void
handle_wl_keyboard_key(void* data,
                       struct wl_keyboard* wl_keyboard,
                       uint32_t serial,
                       uint32_t time,
                       uint32_t key,
                       uint32_t state)
{
    struct state* s = data;

    if (key == KEY_ESC)
        s->keep_running = 0;

    key += 8;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(s->xkb_state, key);

    char buffer[32];
    int size = xkb_state_key_get_utf8(s->xkb_state, key, NULL, 0) + 1;

    char* name = size < (sizeof(buffer)) ? buffer : malloc(size);

    xkb_state_key_get_utf8(s->xkb_state, key, name, size);

    xkb_layout_index_t layout_idx = xkb_state_key_get_layout(s->xkb_state, key);

    if (s->kbd.shift)
        HOG("shift pressed");
    if (s->kbd.alt)
        HOG("alt pressed");
    if (s->kbd.ctrl)
        HOG("ctrl pressed");
    if (s->kbd.super)
        HOG("super pressed");

    HOG("sym: 0x%x, l: %d: %s", sym, layout_idx, name);
}

void
handle_wl_keyboard_modifiers(void* data,
                             struct wl_keyboard* wl_keyboard,
                             uint32_t serial,
                             uint32_t mods_depressed,
                             uint32_t mods_latched,
                             uint32_t mods_locked,
                             uint32_t group)
{
    struct state* state = data;
    xkb_state_update_mask(
      state->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);

    state->kbd.shift = state->kbd.mod_shift != XKB_MOD_INVALID &&
                       xkb_state_mod_index_is_active(state->xkb_state,
                                                     state->kbd.mod_shift,
                                                     XKB_STATE_MODS_EFFECTIVE);

    state->kbd.alt = state->kbd.mod_shift != XKB_MOD_INVALID &&
                     xkb_state_mod_index_is_active(state->xkb_state,
                                                   state->kbd.mod_alt,
                                                   XKB_STATE_MODS_EFFECTIVE);

    state->kbd.ctrl = state->kbd.mod_shift != XKB_MOD_INVALID &&
                      xkb_state_mod_index_is_active(state->xkb_state,
                                                    state->kbd.mod_ctrl,
                                                    XKB_STATE_MODS_EFFECTIVE);

    state->kbd.super = state->kbd.mod_shift != XKB_MOD_INVALID &&
                       xkb_state_mod_index_is_active(state->xkb_state,
                                                     state->kbd.mod_super,
                                                     XKB_STATE_MODS_EFFECTIVE);
}

void
init_seat_devs(struct state* state)
{
    state->pointer = wl_seat_get_pointer(state->seat);

    state->keyboard = wl_seat_get_keyboard(state->seat);

    wl_pointer_add_listener(state->pointer, &wl_pointer_listener, state);

    wl_keyboard_add_listener(state->keyboard, &wl_keyboard_listener, state);
}
