/* SPDX-License-Identifier: GPL-3.0 */
#ifndef DRIVERS_BLOCK_BLOCK_H
#define DRIVERS_BLOCK_BLOCK_H

#include <stdint.h>

int  block_read_sector(uint64_t lba, void *buffer);
int  block_write_sector(uint64_t lba, const void *buffer);
int  block_is_present(void);

#endif /* DRIVERS_BLOCK_BLOCK_H */
