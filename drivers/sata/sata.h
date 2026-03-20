/* SPDX-License-Identifier: GPL-3.0 */
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
