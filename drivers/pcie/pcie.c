/* SPDX-License-Identifier: GPL-2.0 */
#include "pcie.h"
#include "../pci/pci.h"
#include <stdint.h>
/* Keep a small static table of discovered PCIe-capable devices */
#define PCIE_MAX_DEVICES 64
static struct pcie_device g_devices[PCIE_MAX_DEVICES];
static int g_device_count = 0;

int pcie_init(void)
{
    g_device_count = 0;

    /* Limited scan to keep runtime reasonable in QEMU: buses 0..7 */
    for (uint8_t bus = 0; bus < 8; bus++)
    {
        for (uint8_t slot = 0; slot < 32; slot++)
        {
            for (uint8_t func = 0; func < 8; func++)
            {
                uint32_t d0 = pci_config_read32(bus, slot, func, 0);
                if (d0 == 0xFFFFFFFFu) continue;

                /* read type/class */
                uint32_t d2 = pci_config_read32(bus, slot, func, 8);
                uint8_t cls = (d2 >> 24) & 0xFF;
                uint8_t sub = (d2 >> 16) & 0xFF;
                uint8_t pi  = (d2 >> 8) & 0xFF;

                if (g_device_count >= PCIE_MAX_DEVICES) continue;

                struct pcie_device *dev = &g_devices[g_device_count];
                dev->bus = bus; dev->slot = slot; dev->func = func;
                dev->vendor_id = d0 & 0xFFFF;
                dev->device_id = (d0 >> 16) & 0xFFFF;
                dev->class_code = cls;
                dev->subclass = sub;
                dev->prog_if = pi;

                uint32_t hdr_word = pci_config_read32(bus, slot, func, 0x0C);
                dev->header_type = (hdr_word >> 16) & 0xFF;

                /* Read BARs */
                for (int i = 0; i < 6; ++i)
                {
                    dev->bar[i] = pci_get_bar(bus, slot, func, i);
                }

                /* Find PCIe capability offset and read few dwords */
                dev->pcie_cap_offset = 0;
                dev->pcie_cap[0] = dev->pcie_cap[1] = dev->pcie_cap[2] = dev->pcie_cap[3] = 0;

                uint32_t capptr_word = pci_config_read32(bus, slot, func, 0x34 & 0xFC);
                uint8_t cap_ptr = capptr_word & 0xFF;
                while (cap_ptr != 0)
                {
                    uint32_t cap = pci_config_read32(bus, slot, func, cap_ptr & 0xFC);
                    uint8_t id = cap & 0xFF;
                    uint8_t next = (cap >> 8) & 0xFF;
                    if (id == 0x10) /* PCI Express Capability */
                    {
                        dev->pcie_cap_offset = cap_ptr;
                        /* read up to 4 dwords starting at cap_ptr */
                        for (int k = 0; k < 4; ++k)
                        {
                            dev->pcie_cap[k] = pci_config_read32(bus, slot, func, (cap_ptr & 0xFC) + (k * 4));
                        }
                        break;
                    }
                    cap_ptr = next;
                }

                g_device_count++;
            }
        }
    }

    return (g_device_count > 0) ? 0 : -1;
}

int pcie_is_present(void)
{
    return g_device_count > 0;
}

int pcie_device_count(void)
{
    return g_device_count;
}

int pcie_get_device(int idx, struct pcie_device *out)
{
    if (!out) return -1;
    if (idx < 0 || idx >= g_device_count) return -1;
    *out = g_devices[idx];
    return 0;
}

int pcie_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                              uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func)
{
    for (int i = 0; i < g_device_count; ++i)
    {
        struct pcie_device *d = &g_devices[i];
        if (d->class_code == class_code && d->subclass == subclass && d->prog_if == prog_if)
        {
            if (out_bus) *out_bus = d->bus;
            if (out_slot) *out_slot = d->slot;
            if (out_func) *out_func = d->func;
            return 1;
        }
    }
    return 0;
}
