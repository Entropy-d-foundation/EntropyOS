/* SPDX-License-Identifier: GPL-3.0 */
#ifndef USB_HOST_H
#define USB_HOST_H

#include <stdint.h>

/* Abstract USB host helper API. These functions are intentionally minimal
 * and implemented as stubs for xHCI in `drivers/usb/usb_host_xhci.c`.
 * Implement the xHCI transfer handling later; the mass-storage driver
 * will call these functions.
 */

int usb_host_init(void);

/* Find the first Mass Storage interface and return the bulk IN/OUT
 * endpoint addresses and the interface number. Returns 0 on success.
 */
int usb_host_find_mass_storage(uint8_t *out_ep_in, uint8_t *out_ep_out, uint8_t *out_interface);

int usb_host_bulk_out(uint8_t ep, const void *buf, uint32_t len, uint32_t *actual);
int usb_host_bulk_in(uint8_t ep, void *buf, uint32_t len, uint32_t *actual);

/* Returns non-zero if a USB host controller (xHCI) was initialized. */
int usb_host_is_present(void);

#endif /* USB_HOST_H */
