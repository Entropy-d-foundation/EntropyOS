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
#include "../../drivers/usb/usb_host.h"
#include "xhci.h"
#include "../../kernel/console.h"
#include <stdint.h>

static int g_usb_present = 0;

int usb_host_init(void)
{
    if (xhci_init_full() == 0) {
        text("usb_host: xhci init OK (full)\n");
        g_usb_present = 1;
        return 0;
    }

    text("usb_host: xhci_init_full failed, falling back to xhci_init\n");
    if (xhci_init() == 0) {
        text("usb_host: xhci init OK (basic)\n");
        g_usb_present = 1;
        return 0;
    }

    text("usb_host: no xHCI controller found\n");
    g_usb_present = 0;
    return 0; /* not fatal: caller should check presence via usb_host_is_present() */
}

#include "../../drivers/block/block.h"
#include "../../drivers/sata/sata.h"

static struct {
    uint32_t cbw_tag;
    uint8_t  cbw_flags;
    uint8_t  cbw_cdb[16];
    uint32_t cbw_data_len;
    int      cbw_valid;
    int      expect_csw;
    uint8_t  csw_buf[13];
    uint64_t last_lba;
    uint32_t block_size;
} usbms;

/* Probe SATA-backed disk size by exponential search. Returns last LBA or 0 on failure. */
static uint64_t probe_sata_last_lba(void)
{
    uint8_t tmp[512];
    uint64_t probe = 1ULL << 20; /* start at ~1M sectors */
    uint64_t last_good = 0;

    /* If sata not present, fail */
    if (!sata_is_present()) return 0;

    /* Exponential growth until failure or cap */
    for (int i = 0; i < 30; i++) {
        if (sata_read_sector(probe, tmp) == 0) {
            last_good = probe;
            probe <<= 1;
            if (probe == 0) break;
        } else {
            break;
        }
    }

    if (last_good == 0) {
        /* fallback - try small size */
        if (sata_read_sector(0, tmp) != 0) return 0;
        return 0;
    }

    /* Binary search between last_good and probe-1 */
    uint64_t lo = last_good;
    uint64_t hi = probe - 1;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1) / 2;
        if (sata_read_sector(mid, tmp) == 0) lo = mid; else hi = mid - 1;
    }

    return lo;
}

int usb_host_find_mass_storage(uint8_t *out_ep_in, uint8_t *out_ep_out, uint8_t *out_interface)
{
    if (!sata_is_present()) {
        text("usb_host: no backend (sata) available for MS emulation\n");
        return -1;
    }

    /* Fake endpoints and interface numbers for the emulator */
    if (out_ep_in) *out_ep_in = 1;
    if (out_ep_out) *out_ep_out = 2;
    if (out_interface) *out_interface = 0;

    /* Probe once */
    if (usbms.last_lba == 0) {
        usbms.last_lba = probe_sata_last_lba();
        usbms.block_size = 512;
        if (usbms.last_lba == 0) {
            text("usb_host: MS emulation: could not probe SATA disk size\n");
            return -1;
        }
    }

    text("usb_host: MS emulation available (backed by SATA)\n");
    return 0;
}

int usb_host_bulk_out(uint8_t ep, const void *buf, uint32_t len, uint32_t *actual)
{
    (void)ep;
    /* If this is a CBW (31 bytes) handle and store it */
    if (len >= 31 && buf) {
        const uint8_t *b = buf;
        uint32_t sig = (b[0]) | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
        if (sig == 0x43425355u) {
            /* CBW */
            usbms.cbw_valid = 1;
            usbms.cbw_tag = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
            usbms.cbw_data_len = b[8] | (b[9]<<8) | (b[10]<<16) | (b[11]<<24);
            usbms.cbw_flags = b[12];
            uint8_t cb_len = b[14];
            if (cb_len > 16) cb_len = 16;
            for (int i = 0; i < 16; i++) usbms.cbw_cdb[i] = b[15 + i];
            usbms.expect_csw = 0;
            if (actual) *actual = len;
            return 0;
        }
    }

    /* If a data-out phase for WRITE(10) follows a CBW, execute SATA writes */
    if (usbms.cbw_valid && usbms.cbw_flags == 0) {
        uint8_t opcode = usbms.cbw_cdb[0];
        if (opcode == 0x2A) {
            /* WRITE(10): parse LBA and transfer count */
            uint64_t lba = (uint32_t)((usbms.cbw_cdb[2] << 24) | (usbms.cbw_cdb[3] << 16) | (usbms.cbw_cdb[4] << 8) | usbms.cbw_cdb[5]);
            uint32_t cnt = (usbms.cbw_cdb[7] << 8) | usbms.cbw_cdb[8];
            /* Expect len == cnt * block_size */
            const uint8_t *src = buf;
            for (uint32_t i = 0; i < cnt; i++) {
                if (block_write_sector(lba + i, src + (i * usbms.block_size)) != 0) {
                    text("usb_host: MS emu write failed\n");
                    return -1;
                }
            }
            /* Prepare CSW (success) to be read by the guest later */
            usbms.csw_buf[0] = 0x55; usbms.csw_buf[1] = 0x53; usbms.csw_buf[2] = 0x42; usbms.csw_buf[3] = 0x53; /* 'USBS' */
            usbms.csw_buf[4] = (usbms.cbw_tag) & 0xFF;
            usbms.csw_buf[5] = (usbms.cbw_tag >> 8) & 0xFF;
            usbms.csw_buf[6] = (usbms.cbw_tag >> 16) & 0xFF;
            usbms.csw_buf[7] = (usbms.cbw_tag >> 24) & 0xFF;
            usbms.csw_buf[8] = 0; usbms.csw_buf[9] = 0; usbms.csw_buf[10] = 0; /* data residue */
            usbms.csw_buf[11] = 0; /* status: success */
            usbms.expect_csw = 1;
            usbms.cbw_valid = 0;
            if (actual) *actual = len;
            return 0;
        }
    }

    if (actual) *actual = 0;
    return 0; /* No-op for other cases */
}

int usb_host_bulk_in(uint8_t ep, void *buf, uint32_t len, uint32_t *actual)
{
    (void)ep;
    uint8_t *b = buf ? buf : (uint8_t *)0;

    /* If a CBW was outstanding with IN data expected, handle common SCSI ops */
    if (usbms.cbw_valid && (usbms.cbw_flags & 0x80)) {
        uint8_t opcode = usbms.cbw_cdb[0];
        if (opcode == 0x25) {
            /* READ CAPACITY(10) — return 8 bytes [last_lba][blocklen] */
            if (len < 8) return -1;
            uint32_t last_lba = (uint32_t)(usbms.last_lba & 0xFFFFFFFFu);
            b[0] = (last_lba >> 24) & 0xFF;
            b[1] = (last_lba >> 16) & 0xFF;
            b[2] = (last_lba >> 8) & 0xFF;
            b[3] = (last_lba >> 0) & 0xFF;
            uint32_t bl = usbms.block_size;
            b[4] = (bl >> 24) & 0xFF;
            b[5] = (bl >> 16) & 0xFF;
            b[6] = (bl >> 8) & 0xFF;
            b[7] = (bl >> 0) & 0xFF;
            /* Prepare CSW */
            usbms.csw_buf[0] = 0x55; usbms.csw_buf[1] = 0x53; usbms.csw_buf[2] = 0x42; usbms.csw_buf[3] = 0x53;
            usbms.csw_buf[4] = (usbms.cbw_tag) & 0xFF;
            usbms.csw_buf[5] = (usbms.cbw_tag >> 8) & 0xFF;
            usbms.csw_buf[6] = (usbms.cbw_tag >> 16) & 0xFF;
            usbms.csw_buf[7] = (usbms.cbw_tag >> 24) & 0xFF;
            usbms.csw_buf[8] = 0; usbms.csw_buf[9] = 0; usbms.csw_buf[10] = 0;
            usbms.csw_buf[11] = 0;
            usbms.expect_csw = 1;
            usbms.cbw_valid = 0;
            if (actual) *actual = 8;
            return 0;
        }

        if (opcode == 0x28) {
            /* READ(10) — return requested blocks */
            if ((usbms.cbw_cdb[2] & 0x80) != 0) return -1; /* unsupported 64-bit LBA here */
            uint64_t lba = (uint32_t)((usbms.cbw_cdb[2] << 24) | (usbms.cbw_cdb[3] << 16) | (usbms.cbw_cdb[4] << 8) | usbms.cbw_cdb[5]);
            uint32_t cnt = (usbms.cbw_cdb[7] << 8) | usbms.cbw_cdb[8];
            uint32_t to_read = cnt * usbms.block_size;
            if (len < to_read) return -1;
            for (uint32_t i = 0; i < cnt; i++) {
                if (block_read_sector(lba + i, b + (i * usbms.block_size)) != 0) {
                    text("usb_host: MS emu read sector failed\n");
                    return -1;
                }
            }
            /* Prepare CSW */
            usbms.csw_buf[0] = 0x55; usbms.csw_buf[1] = 0x53; usbms.csw_buf[2] = 0x42; usbms.csw_buf[3] = 0x53;
            usbms.csw_buf[4] = (usbms.cbw_tag) & 0xFF;
            usbms.csw_buf[5] = (usbms.cbw_tag >> 8) & 0xFF;
            usbms.csw_buf[6] = (usbms.cbw_tag >> 16) & 0xFF;
            usbms.csw_buf[7] = (usbms.cbw_tag >> 24) & 0xFF;
            usbms.csw_buf[8] = 0; usbms.csw_buf[9] = 0; usbms.csw_buf[10] = 0;
            usbms.csw_buf[11] = 0;
            usbms.expect_csw = 1;
            usbms.cbw_valid = 0;
            if (actual) *actual = to_read;
            return 0;
        }
    }

    /* If the device is expected to supply CSW (status) — provide it */
    if (usbms.expect_csw && buf && len >= 13) {
        for (int i = 0; i < 13; i++) ((uint8_t *)buf)[i] = usbms.csw_buf[i];
        usbms.expect_csw = 0;
        if (actual) *actual = 13;
        return 0;
    }

    if (actual) *actual = 0;
    return 0;
}

int usb_host_is_present(void)
{
    return g_usb_present;
}
