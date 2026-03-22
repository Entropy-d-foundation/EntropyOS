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
#include "xhci_core.h"
#include "xhci.h"
#include "../../kernel/console.h"
#include <stdint.h>
#include <string.h>

/* Simple static ring storage so we don't depend on dynamic alloc yet. */
static struct xhci_trb g_admin_trbs[256];
static struct xhci_ring g_admin_ring;
static int g_core_inited = 0;

int xhci_core_init(void)
{
    uint64_t mmio = xhci_get_mmio_base();
    if (!mmio) {
        text("xhci_core: no xHCI MMIO base\n");
        return -1;
    }

    /* Initialize a simple software ring using the static buffer. */
    memset(g_admin_trbs, 0, sizeof(g_admin_trbs));
    g_admin_ring.trbs = g_admin_trbs;
    g_admin_ring.length = (uint32_t)(sizeof(g_admin_trbs) / sizeof(g_admin_trbs[0]));
    g_admin_ring.enqueue = 0;
    g_admin_ring.cycle = 1;
    g_core_inited = 1;
    text("xhci_core: initialized software ring\n");
    return 0;
}

int xhci_core_submit_noop(void)
{
    if (!g_core_inited) return -1;
    uint32_t idx = g_admin_ring.enqueue;
    struct xhci_trb *t = &g_admin_ring.trbs[idx];
    t->param = 0;
    t->status = 0;
    t->control = (1u << 10) | /* TRB Type: No-op (placeholder) */ 0; 
    /* Advance pointer */
    idx = (idx + 1) % g_admin_ring.length;
    g_admin_ring.enqueue = idx;
    return 0;
}

int xhci_core_bringup(void)
{
    if (!g_core_inited) {
        if (xhci_core_init() != 0) return -1;
    }

    uint64_t mmio = xhci_get_mmio_base();
    if (!mmio) return -1;

    volatile uint8_t *mm8 = (volatile uint8_t *)(uintptr_t)mmio;
    uint8_t caplen = mm8[0x00];
    uint64_t op = mmio + (uint64_t)caplen;

    text("xhci_core: bringup: resetting controller\n");
    if (xhci_reset_controller() != 0) {
        text("xhci_core: reset failed\n");
        /* continue to attempt start */
    }

    if (xhci_start_controller() != 0) {
        text("xhci_core: start failed\n");
        return -1;
    }

    text("xhci_core: controller started; probing ports\n");

    /* Port registers start at op + 0x400 in xHCI spec; each port is 0x10 bytes */
    const uint64_t port_base = op + 0x400;
    for (int i = 0; i < 16; ++i) {
        uint64_t off = port_base + (uint64_t)i * 0x10ULL;
        uint32_t p = *(volatile uint32_t *)(uintptr_t)(off);
        if (p == 0xFFFFFFFFu) {
            /* MMIO read returned all ones — likely no more ports */
            break;
        }
        if (p == 0) {
            /* no status; continue scanning */
            continue;
        }
        /* Log port index and connection bit (bit 0) */
        char buf[64];
        (void)buf;
        if (p & 0x1u) {
            text("xhci_core: port connected\n");
        } else {
            text("xhci_core: port not connected\n");
        }
    }

    return 0;
}
