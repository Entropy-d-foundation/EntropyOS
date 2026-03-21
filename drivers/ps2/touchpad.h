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
#ifndef DRIVERS_TOUCHPAD_H
#define DRIVERS_TOUCHPAD_H

#include <stdint.h>

/* Initialize the PS/2 touchpad/mouse device. Returns 0 on success. */
int ps2_touchpad_init(void);

/* Poll the PS/2 touchpad. Returns 1 if a motion/button event is available and
 * fills dx, dy (signed deltas) and buttons (bitmask: 1=left,2=right,4=middle).
 */
int touchpad_poll(int8_t *dx, int8_t *dy, uint8_t *buttons);

/* Poll with absolute coordinates (like Haiku). Returns 1 if event available.
 * abs_x, abs_y are normalized 0.0-1.0 based on screen_width/height.
 * If screen dimensions are 0, returns relative deltas instead.
 */
int touchpad_poll_absolute(float *abs_x, float *abs_y, uint8_t *buttons,
                           int screen_width, int screen_height);

/* USB touchpad wrapper (optional) */
int usb_touchpad_init(void);
int usb_touchpad_poll(int8_t *dx, int8_t *dy, uint8_t *buttons);
int usb_touchpad_poll_absolute(float *abs_x, float *abs_y, uint8_t *buttons,
                               int screen_width, int screen_height);

/* Diagnostics: return counts of bytes classified as mouse/keyboard since
 * last call (counters are cleared). Useful for diagnosing misclassification.
 */
void ps2_get_and_clear_counters(int *mouse_bytes, int *kbd_bytes);

/* Increment helpers used by PS/2 driver to count classified bytes */
void ps2_inc_mouse_byte(void);
void ps2_inc_kbd_byte(void);

#endif /* DRIVERS_TOUCHPAD_H */
