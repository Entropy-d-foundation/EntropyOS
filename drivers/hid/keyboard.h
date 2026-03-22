/*
    GloamOS
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
