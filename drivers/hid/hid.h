/* SPDX-License-Identifier: GPL-3.0 */
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
