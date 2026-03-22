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
#ifndef USB_PCI_H
#define USB_PCI_H

#include <stdint.h>

/* Read/write PCI configuration space via I/O ports 0xCF8/0xCFC */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/* Find first device matching class/subclass/prog_if. Returns 1 if found and fills bus/slot/func. */
int pci_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                             uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func);

/* Basic initialization hook for legacy PCI support. This is a no-op for
 * now but provides a clear API boundary between the PCI and PCIe code.
 */
void pci_init(void);

/* Check whether a specific PCI device exposes the PCI Express capability
 * (capability ID 0x10). Returns 1 if the device has PCIe capability, 0
 * otherwise. This is a minimal helper used to detect PCIe devices/root
 * complexes without implementing full ECAM access. */
int pci_device_has_pcie(uint8_t bus, uint8_t slot, uint8_t func);
/* Read device BAR (0..5). Returns 64-bit physical base address for memory BARs,
 * or the I/O port base for I/O BARs (lower bits cleared). Returns 0 if BAR
 * is unimplemented/zero. */
uint64_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index);

#endif /* USB_PCI_H */
