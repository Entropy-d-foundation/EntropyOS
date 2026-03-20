/* SPDX-License-Identifier: GPL-2.0 */
#include "mass_storage.h"
#include "../../drivers/usb/usb_host.h"
#include "../../kernel/console.h"
#include "debug_serial.h"
#include <stdint.h>
#include <string.h>

/* USB Mass Storage Bulk-Only Transport (BOT) skeleton.
 * This code provides a small block-device API implemented on top of
 * abstract `usb_host_*` functions. The low-level xHCI transfer logic
 * should be implemented in `drivers/usb/usb_host_xhci.c` later.
 */

struct __attribute__((packed)) cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t cb_length;
    uint8_t cb[16];
};

struct __attribute__((packed)) csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t status;
};

static uint8_t g_ep_in = 0;
static uint8_t g_ep_out = 0;
static uint8_t g_interface = 0;
static uint64_t g_num_blocks = 0;
static uint32_t g_block_size = 512;
static int g_present = 0;

static int send_cbw(uint32_t tag, uint8_t flags, const void *cdb, uint8_t cdb_len, uint32_t data_len)
{
    struct cbw packet;
    memset(&packet, 0, sizeof(packet));
    packet.signature = 0x43425355u;
    packet.tag = tag;
    packet.data_transfer_length = data_len;
    packet.flags = flags;
    packet.lun = 0;
    packet.cb_length = cdb_len;
    if (cdb_len > 16) return -1;
    memcpy(packet.cb, cdb, cdb_len);
    uint32_t actual = 0;
    return usb_host_bulk_out(g_ep_out, &packet, sizeof(packet), &actual);
}

static int recv_csw(uint32_t expected_tag, struct csw *out)
{
    uint32_t actual = 0;
    int r = usb_host_bulk_in(g_ep_in, out, sizeof(*out), &actual);
    if (r != 0) return r;
    if (actual < sizeof(*out)) return -1;
    if (out->signature != 0x53425355u) return -1;
    if (out->tag != expected_tag) return -1;
    return 0;
}

int ms_init(void)
{
    if (g_present) return 0;
    if (usb_host_init() != 0) return -1;
    uint8_t in=0, out=0, ifn=0;
    if (usb_host_find_mass_storage(&in, &out, &ifn) != 0) {
        LOG_ERROR("ms: no mass-storage device found");
        return -1;
    }
    g_ep_in = in;
    g_ep_out = out;
    g_interface = ifn;
    LOG_INFO("ms: endpoints in=0x%x out=0x%x interface=%d", g_ep_in, g_ep_out, g_interface);

    uint8_t cdb[10] = {0x25,0,0,0,0,0,0,0,0,0};
    uint32_t tag = 1;
    if (send_cbw(tag, 0x80, cdb, 10, 8) != 0) { LOG_ERROR("ms: cbw send failed"); return -1; }
    uint8_t data[8];
    uint32_t actual = 0;
    if (usb_host_bulk_in(g_ep_in, data, sizeof(data), &actual) != 0) { LOG_ERROR("ms: readcap data failed"); return -1; }
    struct csw csw;
    if (recv_csw(tag, &csw) != 0) { LOG_ERROR("ms: csw failed"); return -1; }

    uint32_t last_lba = (data[0]<<24)|(data[1]<<16)|(data[2]<<8)|data[3];
    uint32_t block_len = (data[4]<<24)|(data[5]<<16)|(data[6]<<8)|(data[7]<<0);
    g_num_blocks = (uint64_t)last_lba + 1;
    g_block_size = block_len ? block_len : 512;
    g_present = 1;
    LOG_INFO("ms: initialized, blocks=%llu, blocksize=%u", (unsigned long long)g_num_blocks, g_block_size);
    return 0;
}

int ms_is_present(void)
{
    return g_present;
}

int ms_read_blocks(uint64_t lba, uint32_t count, void *buf)
{
    if (!g_present) return -1;
    if (count == 0) return 0;
    if ((lba >> 32) != 0) return -1;
    uint8_t cdb[10];
    memset(cdb,0,sizeof(cdb));
    cdb[0] = 0x28;
    cdb[2] = (uint8_t)((lba >> 24) & 0xFF);
    cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
    cdb[4] = (uint8_t)((lba >> 8) & 0xFF);
    cdb[5] = (uint8_t)((lba >> 0) & 0xFF);
    cdb[7] = (uint8_t)((count >> 8) & 0xFF);
    cdb[8] = (uint8_t)(count & 0xFF);

    uint32_t data_len = count * g_block_size;
    uint32_t tag = 0xA5;
    if (send_cbw(tag, 0x80, cdb, 10, data_len) != 0) return -1;
    uint32_t actual = 0;
    if (usb_host_bulk_in(g_ep_in, buf, data_len, &actual) != 0) return -1;
    struct csw csw;
    if (recv_csw(tag, &csw) != 0) return -1;
    return 0;
}

int ms_write_blocks(uint64_t lba, uint32_t count, const void *buf)
{
    if (!g_present) return -1;
    if (count == 0) return 0;
    if ((lba >> 32) != 0) return -1;

    uint8_t cdb[10];
    memset(cdb,0,sizeof(cdb));
    cdb[0] = 0x2A;
    cdb[2] = (uint8_t)((lba >> 24) & 0xFF);
    cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
    cdb[4] = (uint8_t)((lba >> 8) & 0xFF);
    cdb[5] = (uint8_t)((lba >> 0) & 0xFF);
    cdb[7] = (uint8_t)((count >> 8) & 0xFF);
    cdb[8] = (uint8_t)(count & 0xFF);

    uint32_t data_len = count * g_block_size;
    uint32_t tag = 0xB6;
    if (send_cbw(tag, 0x00, cdb, 10, data_len) != 0) return -1;
    uint32_t actual = 0;
    if (usb_host_bulk_out(g_ep_out, buf, data_len, &actual) != 0) return -1;
    struct csw csw;
    if (recv_csw(tag, &csw) != 0) return -1;
    return 0;
}

uint64_t ms_get_num_blocks(void)
{
    return g_num_blocks;
}

uint32_t ms_get_block_size(void)
{
    return g_block_size;
}
