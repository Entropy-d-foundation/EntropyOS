/* SPDX-License-Identifier: GPL-3.0 */
#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include <stdint.h>

/* Initialize keyboard subsystem (USB preferred, PS/2 fallback). Returns 0 on
 * success, -1 if no keyboard backend is available.
 */
int keyboard_init(void);

/* Scheduler task that polls for key events and prints them to serial. */
void keyboard_task(void *arg, double dt);

/* Return and clear the last scancode received (make code). Returns 1 and
 * fills `scancode` when a scancode is available, otherwise returns 0.
 * The GUI can use this to implement keyboard-driven cursor movement.
 */
int keyboard_get_scancode_and_clear(uint8_t *scancode);

#endif /* HID_KEYBOARD_H */
