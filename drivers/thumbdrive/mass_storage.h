/* SPDX-License-Identifier: GPL-3.0 */
#ifndef MASS_STORAGE_H
#define MASS_STORAGE_H

#include <stdint.h>

/* Minimal USB Mass Storage (Bulk-Only Transport) block device API.
 * This file provides a small block-device style interface that depends
 * on a lower-level USB transfer layer (provided separately).
 */

int ms_init(void);
int ms_is_present(void);
int ms_read_blocks(uint64_t lba, uint32_t count, void *buf);
int ms_write_blocks(uint64_t lba, uint32_t count, const void *buf);
uint64_t ms_get_num_blocks(void);
uint32_t ms_get_block_size(void);

#endif /* MASS_STORAGE_H */
