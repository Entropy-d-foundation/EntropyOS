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
#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>

/* Initialize USB HID subsystem. Returns 0 on success, -1 if no host/controller
 * is available. This is a lightweight abstraction for HID devices.
 */
int usb_hid_init(void);

/* Poll for HID mouse/touchpad data. Returns 1 if a sample available and fills
 * dx, dy and buttons. Returns 0 if no data.
 */
int usb_hid_poll(int8_t *dx, int8_t *dy, uint8_t *buttons);

#endif /* USB_HID_H */
