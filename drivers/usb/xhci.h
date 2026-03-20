/* SPDX-License-Identifier: GPL-3.0 */
#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>

/* Minimal xHCI detection/initialization interface (skeleton)
 * This is a stub/skeleton that detects an xHCI controller and
 * exposes a simple initialized flag. Full xHCI implementation is
 * large; this file is the starting point.
 */

int xhci_init(void);
int xhci_is_present(void);
/* If present, return MMIO base address of xHCI controller (0 if unknown). */
uint64_t xhci_get_mmio_base(void);

/* Find the first xHCI device and return bus/slot/func and BAR0 MMIO base.
 * Returns 0 on success (found) and fills outputs, or -1 if not found.
 */
int xhci_find_device(uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func, uint64_t *out_bar0);

/* Extended helper API (probe + early bringup helper) */
int xhci_init_full(void); /* perform extended probe, reset/start controller */
void xhci_dump_capability(void);
void xhci_dump_operational(void);
int xhci_reset_controller(void);
int xhci_start_controller(void);

#define XHCI_SUCCESS 0
#define XHCI_ERROR   -1

#endif /* XHCI_H */
