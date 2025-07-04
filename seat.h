#pragma once

#include <wayland-client.h>

#include "main.h"

void
init_seat_devs(struct state* state);

static void
handle_wl_pointer_enter(void *data,
                             struct wl_pointer *wl_pointer,
                             uint32_t serial,
                             struct wl_surface *surface,
                             wl_fixed_t surface_x,
                             wl_fixed_t surface_y)
{
}

static void
handle_wl_pointer_leave(void *data,
                             struct wl_pointer *wl_pointer,
                             uint32_t serial,
                             struct wl_surface *surface)
{
}

void
handle_wl_pointer_motion(void *data,
                              struct wl_pointer *wl_pointer,
                              uint32_t time,
                              wl_fixed_t surface_x,
                              wl_fixed_t surface_y);

void
handle_wl_pointer_button(void *data,
                              struct wl_pointer *wl_pointer,
                              uint32_t serial,
                              uint32_t time,
                              uint32_t button,
                              uint32_t state);

void
handle_wl_pointer_axis(void *data,
                            struct wl_pointer *wl_pointer,
                            uint32_t time,
                            uint32_t axis,
                            wl_fixed_t value);

static const struct wl_pointer_listener wl_pointer_listener = {
    .enter = handle_wl_pointer_enter,
    .leave = handle_wl_pointer_leave,
    .motion = handle_wl_pointer_motion,
    .button = handle_wl_pointer_button,
    .axis = handle_wl_pointer_axis,
};


void
handle_wl_keyboard_keymap(void *data,
                               struct wl_keyboard *wl_keyboard,
                               uint32_t format,
                               int32_t fd,
                               uint32_t size);

void 
handle_wl_keyboard_enter(void *data,
                              struct wl_keyboard *wl_keyboard,
                              uint32_t serial,
                              struct wl_surface *surface,
                              struct wl_array *keys);

void 
handle_wl_keyboard_leave(void *data,
                              struct wl_keyboard *wl_keyboard,
                              uint32_t serial,
                              struct wl_surface *surface);

void 
handle_wl_keyboard_key(void *data,
                            struct wl_keyboard *wl_keyboard,
                            uint32_t serial,
                            uint32_t time,
                            uint32_t key,
                            uint32_t state);


void 
handle_wl_keyboard_modifiers(void *data,
                                  struct wl_keyboard *wl_keyboard,
                                  uint32_t serial,
                                  uint32_t mods_depressed,
                                  uint32_t mods_latched,
                                  uint32_t mods_locked,
                                  uint32_t group);

static const struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap = handle_wl_keyboard_keymap,
    .enter = handle_wl_keyboard_enter,
    .leave = handle_wl_keyboard_leave,
    .key = handle_wl_keyboard_key,
    .modifiers = handle_wl_keyboard_modifiers,
};
