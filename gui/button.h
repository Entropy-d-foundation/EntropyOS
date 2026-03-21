/*
    EntropyOS
    Copyright (C) 2025  Gabriel Sîrbu

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef GUI_BUTTON_H
#define GUI_BUTTON_H

#include "../include/efi.h"
#include "icons.h"

typedef void (*button_cb_t)(void);

typedef struct
{
    const Icon *icon;
    int x;
    int y;
    int w;
    int h;
    int pressed;       /* internal state */
    button_cb_t cb;
    u32 rgba;          /* add this for invisible/logical buttons */
} Button;

/* Low-friction API: initialize button subsystem with GOP, update cursor
 * state each frame with `button_update_cursor()`, then call
 * `drawButton(icon, cb, x, y)` which draws a single button at the
 * provided `(x,y)` position and invokes `cb()` when clicked.
 */
void button_init(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
void button_update_cursor(int cursor_x, int cursor_y, uint8_t buttons);
int drawButton(const Icon *icon, button_cb_t cb, int x, int y);
int
drawButtonBox(button_cb_t cb,
              int x, int y, int w, int h, u32 rgba);

#endif /* GUI_BUTTON_H */
