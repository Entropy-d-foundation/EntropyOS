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
#ifndef MASS_STORAGE_H
#define MASS_STORAGE_H

#include <stdint.h>

int ms_init(void);
int ms_is_present(void);
int ms_read_blocks(uint64_t lba, uint32_t count, void *buf);
int ms_write_blocks(uint64_t lba, uint32_t count, const void *buf);
uint64_t ms_get_num_blocks(void);
uint32_t ms_get_block_size(void);

#endif /* MASS_STORAGE_H */
