/* SPDX-License-Identifier: GPL-3.0 */
#ifndef XHCI_CORE_H
#define XHCI_CORE_H

#include <stdint.h>

/* Minimal xHCI core scaffolding: TRB/ring types and tiny helpers.
 * This is the starting point for TRB-based transfer submission and
 * event handling. It purposely avoids interacting with hardware yet
 * so it can be iterated on safely.
 */

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

/* Initialize xHCI core scaffolding. Returns 0 on success (controller present),
 * negative on error (no controller found). This does not perform full
 * controller bring-up — that's planned for later steps. */
int xhci_core_init(void);

/* Submit a no-op TRB to the admin ring (local software ring). Useful for
 * testing ring bookkeeping before implementing real submission to MMIO. */
int xhci_core_submit_noop(void);

/* Perform basic controller bring-up: reset/start and probe port status.
 * Returns 0 on success (controller accessible), negative on error.
 */
int xhci_core_bringup(void);

/* Submit a control transfer to the specified slot/endpoint. This is a
 * high-level helper that will be implemented using TRBs. For now it's a
 * scaffold that returns -1 (not implemented) but provides a stable API.
 */
int xhci_core_submit_control(uint8_t slot_id, uint8_t ep, const void *setup, uint32_t setup_len,
                             void *data, uint32_t data_len, uint32_t timeout_ms);

/* Perform a minimal device enumeration for the device attached to `port_index`.
 * Returns 0 on success and fills `out_slot` with a logical device slot id.
 * Currently a stub that returns -1 (not implemented).
 */
int xhci_core_enumerate_port(int port_index, uint8_t *out_slot);

/* Probe ports and return the first connected port index (1-based), or 0 if none.
 */
int xhci_core_probe_ports(void);

#endif /* XHCI_CORE_H */
