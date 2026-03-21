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
#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>

int xhci_init(void);
int xhci_is_present(void);
uint64_t xhci_get_mmio_base(void);
int xhci_find_device(uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func, uint64_t *out_bar0);
int xhci_init_full(void); /* perform extended probe, reset/start controller */
void xhci_dump_capability(void);
void xhci_dump_operational(void);
int xhci_reset_controller(void);
int xhci_start_controller(void);

#define XHCI_SUCCESS 0
#define XHCI_ERROR   -1

#endif /* XHCI_H */
