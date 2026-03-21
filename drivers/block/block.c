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
#include "block.h"
#include "../thumbdrive/mass_storage.h"
#include "../sata/sata.h"
#include "../../kernel/console.h"
#include <stdint.h>

int block_read_sector(uint64_t lba, void *buffer)
{
    /* Prefer SATA when available; only fall back to USB mass-storage when
     * no SATA backend is active. This avoids accidentally targeting a
     * transient or emulated USB backend when the real disk is on AHCI. */
    if (sata_is_present()) {
        return sata_read_sector(lba, buffer);
    }

    if (ms_is_present()) {
        if (ms_read_blocks(lba, 1, buffer) != 0) {
            text("block: ms_read_blocks failed\n");
            return -1;
        }
        return 0;
    }

    return -1;
}

int block_write_sector(uint64_t lba, const void *buffer)
{
    if (sata_is_present()) {
        return sata_write_sector(lba, buffer);
    }

    if (ms_is_present()) {
        if (ms_write_blocks(lba, 1, buffer) != 0) {
            text("block: ms_write_blocks failed\n");
            return -1;
        }
        return 0;
    }

    return -1;
}

int block_is_present(void)
{
    return sata_is_present() || ms_is_present();
}
