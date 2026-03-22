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
#ifndef XHCI_CORE_H
#define XHCI_CORE_H

#include <stdint.h>


struct xhci_trb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
};

struct xhci_ring {
    struct xhci_trb *trbs;
    uint32_t length; /* number of TRBs */
    uint32_t enqueue; /* next index to enqueue */
    uint8_t cycle;
};
int xhci_core_init(void);
int xhci_core_submit_noop(void);
int xhci_core_bringup(void);
int xhci_core_submit_control(uint8_t slot_id, uint8_t ep, const void *setup, uint32_t setup_len,
                             void *data, uint32_t data_len, uint32_t timeout_ms);
int xhci_core_enumerate_port(int port_index, uint8_t *out_slot);
int xhci_core_probe_ports(void);

#endif /* XHCI_CORE_H */
