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
#include "pci.h"
#include <stdint.h>

/* PCI config I/O ports */
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline void outl(uint16_t port, uint32_t val)
{
        __asm__ volatile ("outl %0, %1" : : "a" (val), "Nd" (port));
}

static inline uint32_t inl(uint16_t port)
{
        uint32_t ret;
        __asm__ volatile ("inl %1, %0" : "=a" (ret) : "Nd" (port));
        return ret;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
        uint32_t addr = (uint32_t)(0x80000000u |
                                   ((uint32_t)bus << 16) |
                                   ((uint32_t)slot << 11) |
                                   ((uint32_t)func << 8) |
                                   (offset & 0xFC));
        outl(PCI_CONFIG_ADDR, addr);
        return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
        uint32_t addr = (uint32_t)(0x80000000u |
                                   ((uint32_t)bus << 16) |
                                   ((uint32_t)slot << 11) |
                                   ((uint32_t)func << 8) |
                                   (offset & 0xFC));
        outl(PCI_CONFIG_ADDR, addr);
        outl(PCI_CONFIG_DATA, value);
}

int pci_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                             uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func)
{
        for (int bus = 0; bus < 256; bus++)
        {
                for (int slot = 0; slot < 32; slot++)
                {
                        for (int func = 0; func < 8; func++)
                        {
                                uint32_t d0 = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0);
                                if (d0 == 0xFFFFFFFFu) continue;
                                uint32_t d2 = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 8);
                                uint8_t cls = (d2 >> 24) & 0xFF;
                                uint8_t sub = (d2 >> 16) & 0xFF;
                                uint8_t pi  = (d2 >> 8) & 0xFF;
                                if (cls == class_code && sub == subclass && pi == prog_if)
                                {
                                        if (out_bus) *out_bus = (uint8_t)bus;
                                        if (out_slot) *out_slot = (uint8_t)slot;
                                        if (out_func) *out_func = (uint8_t)func;
                                        return 1;
                                }
                        }
                }
        }
        return 0;
}

void pci_init(void)
{
        /* Currently no global setup required for legacy IO-based PCI access. */
}

uint64_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index)
{
        if (bar_index < 0 || bar_index > 5) return 0;
        uint8_t off = 0x10 + (bar_index * 4);
        uint32_t low = pci_config_read32(bus, slot, func, off);
        if (low == 0 || low == 0xFFFFFFFFu) return 0;

        /* I/O BAR (bit 0 == 1) */
        if (low & 0x1)
        {
                uint64_t io_base = (uint64_t)(low & ~0x3u);
                return io_base;
        }

        /* Memory BAR */
        uint32_t type = (low >> 1) & 0x3;
        if (type == 0x2)
        {
                /* 64-bit BAR: read the next BAR for high dword */
                uint32_t high = pci_config_read32(bus, slot, func, off + 4);
                uint64_t addr = ((uint64_t)high << 32) | (uint64_t)(low & ~0xFul);
                return addr;
        }

        /* 32-bit memory BAR */
        return (uint64_t)(low & ~0xFul);
}

int pci_device_has_pcie(uint8_t bus, uint8_t slot, uint8_t func)
{
        /* Read status register to see if capabilities list is present */
        uint32_t d1 = pci_config_read32(bus, slot, func, 4);
        uint16_t status = (d1 >> 16) & 0xFFFF;
        if (!(status & 0x10)) /* bit 4 = Capabilities List */
                return 0;

        /* Capability pointer lives at offset 0x34 for type 0 headers */
        uint32_t capptr_word = pci_config_read32(bus, slot, func, 0x34 & 0xFC);
        uint8_t cap_ptr = capptr_word & 0xFF;
        while (cap_ptr != 0)
        {
                uint32_t cap = pci_config_read32(bus, slot, func, cap_ptr & 0xFC);
                uint8_t id = cap & 0xFF;
                uint8_t next = (cap >> 8) & 0xFF;
                if (id == 0x10) /* PCI Express Capability ID */
                        return 1;
                cap_ptr = next;
        }
        return 0;
}
