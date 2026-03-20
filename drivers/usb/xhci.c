/* SPDX-License-Identifier: GPL-2.0 */
#include "xhci.h"
#include "../pci/pci.h"
#include "../pcie/pcie.h"
#include <stdint.h>
#include "../../init/main.h"
#include "../../kernel/console.h"
#include "debug_serial.h"

static void serial_print_hex64(uint64_t v)
{
        /* Print a 64-bit hex value via our logging API */
        serial_printf("0x%016llx\n", (unsigned long long)v);
}

static void xhci_try_start(uint64_t mmio_base)
{
        volatile uint8_t *mmio8 = (volatile uint8_t *)(uintptr_t)mmio_base;
        uint8_t caplen = mmio8[0x00];
        uint64_t op = mmio_base + (uint64_t)caplen;
        uint32_t usbcmd = *(volatile uint32_t *)(uintptr_t)(op + 0x00);
        uint32_t usbsts = *(volatile uint32_t *)(uintptr_t)(op + 0x04);

        serial_print_hex64(op);
        serial_print_hex64((u64)usbcmd);
        serial_print_hex64((u64)usbsts);

        /* USBSTS bit0 == HCHalted (1 == halted). If halted, try to start. */
        if (usbsts & 0x1u)
        {
                /* debug text removed */
                usbcmd |= 0x1u; /* set Run bit */
                *(volatile uint32_t *)(uintptr_t)(op + 0x00) = usbcmd;
                /* Poll for HCHalted -> 0 */
                for (int i = 0; i < 1000; ++i)
                {
                        usbsts = *(volatile uint32_t *)(uintptr_t)(op + 0x04);
                        if ((usbsts & 0x1u) == 0)
                        {
                                /* debug text removed */
                                return;
                        }
                }
                /* debug text removed */
        }
        else
        {
                /* debug text removed */
        }
}

/* Very small xHCI presence check: find PCI device class 0x0C (Serial Bus),
 * subclass 0x03 (USB controller), prog-if 0x30 (xHCI). If found, we consider
 * xHCI present. Full driver initialization is beyond this step and can be
 * implemented later in stages.
 */

static int g_xhci_present = 0;
static uint64_t g_xhci_mmio = 0;

/* Cached capability/operational pointers */
static uint8_t g_caplength = 0;
static uint64_t g_op_base = 0;

/* Simple helpers to read/write MMIO */
static inline uint32_t xhci_mmio_read32(uint64_t base, uint64_t off)
{
        return *(volatile uint32_t *)(uintptr_t)(base + off);
}

static inline void xhci_mmio_write32(uint64_t base, uint64_t off, uint32_t v)
{
        *(volatile uint32_t *)(uintptr_t)(base + off) = v;
}

static inline uint64_t xhci_mmio_read64(uint64_t base, uint64_t off)
{
        return *(volatile uint64_t *)(uintptr_t)(base + off);
}

/* Read many capability/operator dwords for diagnostics */
void xhci_dump_capability(void)
{
        if (!g_xhci_mmio) { /* debug text removed */ return; }
        uint64_t base = g_xhci_mmio;
        serial_print_hex64((u64)*(volatile u8 *)(uintptr_t)base);
        for (u64 off = 0; off < 0x40; off += 4)
        {
                u32 v = xhci_mmio_read32(base, off);
                serial_print_hex64(off); serial_print_hex64((uint64_t)v);
        }
}

void xhci_dump_operational(void)
{
        if (!g_op_base) { /* debug text removed */ return; }
        uint64_t op = g_op_base;
        for (u64 off = 0; off < 0x40; off += 4)
        {
                u32 v = xhci_mmio_read32(op, off);
                serial_print_hex64(off); serial_print_hex64((u64)v);
        }
}

int xhci_init(void)
{
        extern int pcie_is_present(void);
        uint8_t bus, slot, func;

        /* If PCIe detector flagged presence earlier, try to locate the xHCI
         * device and read its BAR. */
        if (pcie_is_present())
        {
                /* debug text removed */
                if (pci_find_device_by_class(0x0C, 0x03, 0x30, &bus, &slot, &func))
                {
                        uint64_t bar0 = pci_get_bar(bus, slot, func, 0);
                        if (bar0)
                        {
                                volatile uint8_t *mmio8 = (volatile uint8_t *)(uintptr_t)bar0;
                                /* CAPLENGTH at offset 0x00 should be reasonable (>= 0x20) */
                                uint8_t caplen = mmio8[0x00];
                                serial_print_hex64((u64)caplen);
                                /* read first capability dwords */
                                uint32_t cap0 = *(volatile uint32_t *)(uintptr_t)(bar0 + 0x00);
                                uint32_t cap1 = *(volatile uint32_t *)(uintptr_t)(bar0 + 0x04);
                                uint32_t cap2 = *(volatile uint32_t *)(uintptr_t)(bar0 + 0x08);
                                serial_print_hex64((u64)cap0); serial_print_hex64((u64)cap1); serial_print_hex64((u64)cap2);
                                if (caplen >= 0x20)
                                {
                                                g_xhci_mmio = bar0;
                                                g_caplength = caplen;
                                                g_op_base = bar0 + (uint64_t)caplen;
                                                g_xhci_present = 1;
                                                /* debug text removed */
                                                serial_print_hex64(g_op_base);
                                                xhci_try_start(bar0);
                                                return 0;
                                }
                                /* debug text removed */
                        }
                        else
                        {
                                /* debug text removed */
                        }
                }
                else
                {
                        /* debug text removed */
                }
        }

        /* Fall back to a limited legacy scan to find an xHCI device and its BAR */
        for (uint8_t b = 0; b < 8; b++)
        {
                for (uint8_t s = 0; s < 32; s++)
                {
                        for (uint8_t f = 0; f < 8; f++)
                        {
                                uint32_t d0 = pci_config_read32(b, s, f, 0);
                                if (d0 == 0xFFFFFFFFu) continue;
                                uint32_t d2 = pci_config_read32(b, s, f, 8);
                                uint8_t cls = (d2 >> 24) & 0xFF;
                                uint8_t sub = (d2 >> 16) & 0xFF;
                                uint8_t pi  = (d2 >> 8) & 0xFF;
                                if (cls == 0x0C && sub == 0x03 && pi == 0x30)
                                {
                                        uint64_t bar0 = pci_get_bar(b, s, f, 0);
                                        if (!bar0) continue;
                                        volatile uint8_t *mmio8 = (volatile uint8_t *)(uintptr_t)bar0;
                                        uint8_t caplen = mmio8[0x00];
                                        if (caplen >= 0x20)
                                        {
                                                g_xhci_mmio = bar0;
                                                g_caplength = caplen;
                                                g_op_base = bar0 + (uint64_t)caplen;
                                                g_xhci_present = 1;
                                                serial_print_hex64(g_op_base);
                                                xhci_try_start(bar0);
                                                return 0;
                                        }
                                }
                        }
                }
        }

        g_xhci_present = 0;
        g_xhci_mmio = 0;
        return -1;
}

int xhci_is_present(void)
{
        return g_xhci_present;
}

uint64_t xhci_get_mmio_base(void)
{
        return g_xhci_mmio;
}

int xhci_find_device(uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func, uint64_t *out_bar0)
{
        uint8_t bus=0, slot=0, func=0;

        /* Try PCIe-detected path first (fast) */
        if (pci_find_device_by_class(0x0C, 0x03, 0x30, &bus, &slot, &func))
        {
                uint64_t bar0 = pci_get_bar(bus, slot, func, 0);
                if (bar0)
                {
                        volatile uint8_t *mmio8 = (volatile uint8_t *)(uintptr_t)bar0;
                        uint8_t caplen = mmio8[0x00];
                                        if (caplen >= 0x20)
                        {
                                if (out_bus) *out_bus = bus;
                                if (out_slot) *out_slot = slot;
                                if (out_func) *out_func = func;
                                if (out_bar0) *out_bar0 = bar0;
                                return 0;
                        }
                }
        }

        /* Fallback: limited legacy scan (0..7) */
        for (uint8_t b = 0; b < 8; b++)
        {
                for (uint8_t s = 0; s < 32; s++)
                {
                        for (uint8_t f = 0; f < 8; f++)
                        {
                                uint32_t d0 = pci_config_read32(b, s, f, 0);
                                if (d0 == 0xFFFFFFFFu) continue;
                                uint32_t d2 = pci_config_read32(b, s, f, 8);
                                uint8_t cls = (d2 >> 24) & 0xFF;
                                uint8_t sub = (d2 >> 16) & 0xFF;
                                uint8_t pi  = (d2 >> 8) & 0xFF;
                                if (cls == 0x0C && sub == 0x03 && pi == 0x30)
                                {
                                        uint64_t bar0 = pci_get_bar(b, s, f, 0);
                                        if (!bar0) continue;
                                        volatile uint8_t *mmio8 = (volatile uint8_t *)(uintptr_t)bar0;
                                        uint8_t caplen = mmio8[0x00];
                                        if (caplen >= 0x20)
                                        {
                                                if (out_bus) *out_bus = b;
                                                if (out_slot) *out_slot = s;
                                                if (out_func) *out_func = f;
                                                if (out_bar0) *out_bar0 = bar0;
                                                return 0;
                                        }
                                }
                        }
                }
        }

        return -1;
}

/* Extended init: perform a fuller probe and attempt robust reset/start sequence */
int xhci_init_full(void)
{
        /* If we already have a cached mmio, dump and attempt start/reset */
        if (g_xhci_present && g_xhci_mmio)
        {
                xhci_dump_capability();
                xhci_dump_operational();
                if (xhci_reset_controller() != XHCI_SUCCESS)
                {
                        /* debug text removed */
                }
                if (xhci_start_controller() != XHCI_SUCCESS)
                {
                        /* debug text removed */
                        return XHCI_ERROR;
                }
                /* debug text removed */
                return XHCI_SUCCESS;
        }

        /* Try to find via PCIe enumerator first */
        if (pcie_is_present())
        {
                int n = pcie_device_count();
                for (int i = 0; i < n; ++i)
                {
                        struct pcie_device d;
                        if (pcie_get_device(i, &d) == 0)
                        {
                                if (d.class_code == 0x0C && d.subclass == 0x03 && d.prog_if == 0x30)
                                {
                                        g_xhci_mmio = d.bar[0];
                                        if (!g_xhci_mmio) continue;
                                        g_caplength = *(volatile uint8_t *)(uintptr_t)g_xhci_mmio;
                                        g_op_base = g_xhci_mmio + (uint64_t)g_caplength;
                                        g_xhci_present = 1;
                                        /* debug text removed */
                                        return xhci_init_full();
                                }
                        }
                }
        }

        /* Fallback to existing init */
        return xhci_init();
}

int xhci_reset_controller(void)
{
        if (!g_op_base) { /* debug text removed */ return XHCI_ERROR; }
        uint64_t op = g_op_base;
        uint32_t usbcmd = xhci_mmio_read32(op, 0x00);
        /* Set Host Controller Reset (HCRST) bit (bit 1) */
        /* debug text removed */
        usbcmd |= 0x2u;
        xhci_mmio_write32(op, 0x00, usbcmd);
        /* wait for HCRST to clear */
        for (int i = 0; i < 5000; ++i)
        {
                uint32_t now = xhci_mmio_read32(op, 0x00);
                if (!(now & 0x2u))
                {
                        /* debug text removed */
                        return XHCI_SUCCESS;
                }
        }
        /* debug text removed */
        return XHCI_ERROR;
}

int xhci_start_controller(void)
{
        if (!g_op_base) { /* debug text removed */ return XHCI_ERROR; }
        uint64_t op = g_op_base;
        uint32_t usbsts = xhci_mmio_read32(op, 0x04);
        uint32_t usbcmd = xhci_mmio_read32(op, 0x00);
        serial_print_hex64(usbsts); serial_print_hex64(usbcmd);
        /* If halted, set run */
        if (usbsts & 0x1u)
        {
                /* debug text removed */
                usbcmd |= 0x1u;
                xhci_mmio_write32(op, 0x00, usbcmd);
                for (int i = 0; i < 2000; ++i)
                {
                        usbsts = xhci_mmio_read32(op, 0x04);
                        if ((usbsts & 0x1u) == 0)
                        {
                                /* debug text removed */
                                return XHCI_SUCCESS;
                        }
                }
                                /* debug text removed */
                return XHCI_ERROR;
        }
        /* debug text removed */
        return XHCI_SUCCESS;
}
