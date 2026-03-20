/* SPDX-License-Identifier: GPL-3.0 */
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
