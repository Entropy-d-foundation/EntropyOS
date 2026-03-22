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
#ifndef SATA_H
#define SATA_H

#include <stdint.h>

extern struct pcie_device sata_controller;
extern int sata_controller_found;

int      sata_detect(void);
int      sata_read_sector(uint64_t lba, void *buffer);
int      sata_write_sector(uint64_t lba, const void *buffer);
int      sata_is_present(void);
int      sata_init(void);

/* Retrieve disk capacity in number of 512-byte sectors. Returns 0 on success */
int      sata_get_num_blocks(uint64_t *out_blocks);

/* Issue ATA FLUSH CACHE to the device to force host-side persistence. */
int      sata_flush_cache(void);

#endif
