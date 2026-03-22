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
#ifndef PCIE_H
#define PCIE_H

#include <stdint.h>

/* Representation of a discovered PCIe-capable device (minimal). */
struct pcie_device {
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t class_code;
	uint8_t subclass;
	uint8_t prog_if;
	uint8_t header_type;
	uint64_t bar[6];
	uint8_t pcie_cap_offset; /* 0 if none */
	uint32_t pcie_cap[4]; /* first 4 dwords of PCIe capability */
};

/* Minimal PCIe management API */
int pcie_init(void);
int pcie_is_present(void);

/* Enumerate and return number of PCIe-capable devices discovered. */
int pcie_device_count(void);
/* Copy device info at index into out (0-based). Returns 0 on success. */
int pcie_get_device(int idx, struct pcie_device *out);
/* Find first PCIe device matching class/sub/prog-if; returns 1 and fills out_bus/out_slot/out_func, else 0. */
int pcie_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
							  uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func);

#endif /* PCIE_H */
