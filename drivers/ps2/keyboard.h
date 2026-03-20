/* SPDX-License-Identifier: GPL-3.0 */
#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include <stdint.h>

/* Minimal PS/2 keyboard interface (poll-only) */
int ps2_keyboard_init(void);
int ps2_keyboard_poll(uint8_t *scancode);

#endif /* PS2_KEYBOARD_H */
