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

#include "../../drivers/block/block.h"
#include "../../drivers/sata/sata.h"
#include "../../kernel/console.h"
#include "fat32.h"
#include <stdint.h>

/* Global filesystem state */
static fat32_fs_t g_fs;

/* Partition offset (LBA where partition starts) */
static uint64_t g_partition_offset = 0;

/* Cached free cluster count for performance */
static uint32_t g_free_clusters = 0xFFFFFFFF;

/* Sector buffer for I/O operations */
static uint8_t sector_buffer[512] __attribute__((aligned(16)));

/* Set partition offset */
void
fat32_set_partition_offset(uint64_t offset)
{
        g_partition_offset = offset;
}

/* Memory comparison */
static int
memcmp(const void *s1, const void *s2, uint32_t n)
{
        const uint8_t *p1 = s1, *p2 = s2;
        for (uint32_t i = 0; i < n; i++)
        {
                if (p1[i] != p2[i])
                        return p1[i] - p2[i];
        }
        return 0;
}

/* Memory copy */
static void
memcpy(void *dest, const void *src, uint32_t n)
{
        uint8_t *d = dest;
        const uint8_t *s = src;
        for (uint32_t i = 0; i < n; i++)
                d[i] = s[i];
}

/* Memory set */
static void
memset(void *s, int c, uint32_t n)
{
        uint8_t *p = s;
        for (uint32_t i = 0; i < n; i++)
                p[i] = (uint8_t)c;
}

/* String length */
static uint32_t
strlen(const char *s)
{
        uint32_t len = 0;
        while (s[len]) len++;
        return len;
}

/* Convert filename to FAT 8.3 format */
static void
name_to_fat(const char *name, uint8_t *fat_name)
{
        memset(fat_name, ' ', 11);

        int i = 0, j = 0;
        while (name[i] && name[i] != '.' && j < 8)
        {
                fat_name[j++] = (name[i] >= 'a' && name[i] <= 'z') ?
                                name[i] - 32 : name[i];
                i++;
        }

        if (name[i] == '.')
        {
                i++;
                j = 8;
                while (name[i] && j < 11)
                {
                        fat_name[j++] =
                                (name[i] >= 'a' && name[i] <= 'z') ?
                                name[i] - 32 : name[i];
                        i++;
                }
        }
}

/* Convert cluster number to LBA */
uint32_t
fat32_cluster_to_lba(uint32_t cluster)
{
        return g_fs.cluster_begin_lba +
               ((cluster - 2) * g_fs.sectors_per_cluster);
}

/* Read a cluster */
int
fat32_read_cluster(uint32_t cluster, void *buffer)
{
        uint32_t lba = fat32_cluster_to_lba(cluster);
        uint8_t *buf = buffer;

        for (uint32_t i = 0; i < g_fs.sectors_per_cluster; i++)
        {
                if (block_read_sector(g_partition_offset + lba + i,
                    buf + (i * 512)) != 0)
                        return -1;
        }
        return 0;
}

/* Write a cluster */
int
fat32_write_cluster(uint32_t cluster, const void *buffer)
{
        uint32_t lba = fat32_cluster_to_lba(cluster);
        const uint8_t *buf = buffer;

        for (uint32_t i = 0; i < g_fs.sectors_per_cluster; i++)
        {
                if (block_write_sector(g_partition_offset + lba + i,
                    buf + (i * 512)) != 0)
                        return -1;
        }
        return 0;
}

/* Get next cluster from FAT
 * FIX: use *(uint32_t*)(sector_buffer + entry_offset) instead of
 *      fat_table[entry_offset / 4] which double-divided by 4. */
uint32_t
fat32_get_next_cluster(uint32_t cluster)
{
        uint32_t fat_offset  = cluster * 4;
        uint32_t fat_sector  = g_fs.fat_begin_lba + (fat_offset / 512);
        uint32_t entry_offset = fat_offset % 512;

        if (block_read_sector(g_partition_offset + fat_sector,
            sector_buffer) != 0)
                return FAT32_EOC;

        uint32_t next = *(uint32_t *)(sector_buffer + entry_offset)
                        & 0x0FFFFFFF;
        return next;
}

/* Set next cluster in FAT
 * FIX: same entry_offset indexing fix as fat32_get_next_cluster. */
int
fat32_set_next_cluster(uint32_t cluster, uint32_t next)
{
        uint32_t fat_offset   = cluster * 4;
        uint32_t fat_sector   = g_fs.fat_begin_lba + (fat_offset / 512);
        uint32_t entry_offset = fat_offset % 512;

        if (block_read_sector(g_partition_offset + fat_sector,
            sector_buffer) != 0)
                return -1;

        uint32_t old = *(uint32_t *)(sector_buffer + entry_offset);
        uint32_t val = (old & 0xF0000000) | (next & 0x0FFFFFFF);
        *(uint32_t *)(sector_buffer + entry_offset) = val;

        if (block_write_sector(g_partition_offset + fat_sector,
            sector_buffer) != 0)
                return -1;

        /* Update backup FAT if present */
        if (g_fs.num_fats > 1)
        {
                uint32_t backup_sector = fat_sector + g_fs.fat_size;
                if (block_write_sector(g_partition_offset + backup_sector,
                    sector_buffer) != 0)
                        return -1;
        }

        return 0;
}

/* Update FSInfo sector */
static int
fat32_update_fsinfo(uint32_t free_count, uint32_t next_free)
{
        if (block_read_sector(g_partition_offset + 1, sector_buffer) != 0)
                return -1;

        if (sector_buffer[0] != 0x52 || sector_buffer[1] != 0x52 ||
            sector_buffer[2] != 0x61 || sector_buffer[3] != 0x41)
                return -1;

        if (free_count != 0xFFFFFFFF)
        {
                sector_buffer[488] = free_count & 0xFF;
                sector_buffer[489] = (free_count >> 8) & 0xFF;
                sector_buffer[490] = (free_count >> 16) & 0xFF;
                sector_buffer[491] = (free_count >> 24) & 0xFF;
        }

        if (next_free != 0xFFFFFFFF)
        {
                sector_buffer[492] = next_free & 0xFF;
                sector_buffer[493] = (next_free >> 8) & 0xFF;
                sector_buffer[494] = (next_free >> 16) & 0xFF;
                sector_buffer[495] = (next_free >> 24) & 0xFF;
        }

        if (block_write_sector(g_partition_offset + 1, sector_buffer) != 0)
                return -1;

        block_write_sector(g_partition_offset + 7, sector_buffer);
        return 0;
}

/* Allocate a new cluster */
uint32_t
fat32_allocate_cluster(void)
{
        for (uint32_t cluster = 2;
             cluster < g_fs.total_clusters + 2;
             cluster++)
        {
                uint32_t next = fat32_get_next_cluster(cluster);
                if (next == FAT32_FREE)
                {
                        if (fat32_set_next_cluster(cluster, FAT32_EOC) == 0)
                        {
                                if (g_free_clusters != 0xFFFFFFFF &&
                                    g_free_clusters > 0)
                                        g_free_clusters--;
                                return cluster;
                        }
                }
        }
        return 0;
}

/* Free a cluster chain starting at 'cluster' */
int
fat32_free_cluster(uint32_t cluster)
{
        int result = fat32_set_next_cluster(cluster, FAT32_FREE);
        if (result == 0)
        {
                if (g_free_clusters != 0xFFFFFFFF)
                        g_free_clusters++;
        }
        return result;
}

/* -------------------------------------------------------------------------
 * fat32_find_entry
 * Walk the full cluster chain of dir_cluster looking for fat_name.
 * FIX: previously only read the first cluster, missing entries in large dirs.
 * ------------------------------------------------------------------------- */
static int
fat32_find_entry(uint32_t dir_cluster, const uint8_t *fat_name,
                 fat32_dir_entry_t *out_entry, uint32_t *out_cluster,
                 uint32_t *out_index)
{
        static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));

        uint32_t cur = dir_cluster;
        while (cur >= 2 && cur < FAT32_EOC)
        {
                if (fat32_read_cluster(cur, cluster_buffer) != 0)
                        return -1;

                fat32_dir_entry_t *entries =
                        (fat32_dir_entry_t *)cluster_buffer;
                uint32_t epc = (g_fs.sectors_per_cluster * 512) /
                               sizeof(fat32_dir_entry_t);

                for (uint32_t i = 0; i < epc; i++)
                {
                        if (entries[i].name[0] == 0x00)
                                return -1; /* end of directory */
                        if (entries[i].name[0] == 0xE5)
                                continue;  /* deleted */
                        if (entries[i].attr & ATTR_VOLUME_ID)
                                continue;

                        if (memcmp(entries[i].name, fat_name, 11) == 0)
                        {
                                if (out_entry)
                                        memcpy(out_entry, &entries[i],
                                               sizeof(fat32_dir_entry_t));
                                if (out_cluster)
                                        *out_cluster = cur;
                                if (out_index)
                                        *out_index = i;
                                return 0;
                        }
                }

                cur = fat32_get_next_cluster(cur);
        }

        return -1;
}

/* -------------------------------------------------------------------------
 * fat32_format
 * FIX: removed dead #ifdef __UEFI__ duplicate flush block, fixed delay value.
 * ------------------------------------------------------------------------- */
int
fat32_format(uint64_t total_sectors, const char *volume_label)
{
        if (!block_is_present())
        {
                text("FAT32: Block device not initialized\n");
                return -1;
        }

        text("FAT32: Formatting partition...\n");

        uint32_t reserved_sectors   = 32;
        uint32_t num_fats           = 2;
        uint32_t sectors_per_cluster = 8; /* 4KB clusters */

        uint32_t tmp1 = (uint32_t)total_sectors - reserved_sectors;
        uint32_t tmp2 = (sectors_per_cluster * 128) + num_fats;
        uint32_t fat_size = (tmp1 + tmp2 - 1) / tmp2;

        /* Build boot sector */
        fat32_boot_sector_t *boot = (fat32_boot_sector_t *)sector_buffer;
        memset(boot, 0, 512);

        boot->jmp[0] = 0xEB;
        boot->jmp[1] = 0x58;
        boot->jmp[2] = 0x90;

        memcpy(boot->oem, "MSWIN4.1", 8);

        boot->bytes_per_sector   = 512;
        boot->sectors_per_cluster = sectors_per_cluster;
        boot->reserved_sectors   = reserved_sectors;
        boot->num_fats           = num_fats;
        boot->root_entries       = 0;
        boot->total_sectors_16   = 0;
        boot->media_type         = 0xF8;
        boot->fat_size_16        = 0;
        boot->sectors_per_track  = 63;
        boot->num_heads          = 255;
        boot->hidden_sectors     = 0;
        boot->total_sectors_32   = (uint32_t)total_sectors;

        boot->fat_size_32  = fat_size;
        boot->ext_flags    = 0;
        boot->fs_version   = 0;
        boot->root_cluster = 2;
        boot->fs_info      = 1;
        boot->backup_boot  = 6;

        memset(boot->reserved, 0, 12);

        boot->drive_number   = 0x80;
        boot->reserved1      = 0;
        boot->boot_signature = 0x29;
        boot->volume_id      = 0x12345678;

        if (volume_label)
        {
                memset(boot->volume_label, ' ', 11);
                int len = (int)strlen(volume_label);
                if (len > 11) len = 11;
                for (int i = 0; i < len; i++)
                {
                        boot->volume_label[i] =
                                (volume_label[i] >= 'a' &&
                                 volume_label[i] <= 'z') ?
                                volume_label[i] - 32 :
                                volume_label[i];
                }
        }
        else
        {
                memcpy(boot->volume_label, "NO NAME    ", 11);
        }

        memcpy(boot->fs_type, "FAT32   ", 8);

        sector_buffer[510] = 0x55;
        sector_buffer[511] = 0xAA;

        if (block_write_sector(g_partition_offset + 0, sector_buffer) != 0)
        {
                text("FAT32: Failed to write boot sector\n");
                return -1;
        }

        if (block_write_sector(g_partition_offset + 6, sector_buffer) != 0)
        {
                text("FAT32: Failed to write backup boot\n");
                return -1;
        }

        /* FSInfo sector */
        memset(sector_buffer, 0, 512);

        sector_buffer[0] = 0x52;
        sector_buffer[1] = 0x52;
        sector_buffer[2] = 0x61;
        sector_buffer[3] = 0x41;

        sector_buffer[484] = 0x72;
        sector_buffer[485] = 0x72;
        sector_buffer[486] = 0x41;
        sector_buffer[487] = 0x61;

        uint32_t data_sectors   = (uint32_t)total_sectors -
                                  (reserved_sectors + (num_fats * fat_size));
        uint32_t total_clusters = data_sectors / sectors_per_cluster;
        uint32_t free_clusters  = total_clusters - 1;

        sector_buffer[488] = free_clusters & 0xFF;
        sector_buffer[489] = (free_clusters >> 8)  & 0xFF;
        sector_buffer[490] = (free_clusters >> 16) & 0xFF;
        sector_buffer[491] = (free_clusters >> 24) & 0xFF;

        /* Next free hint: cluster 3 */
        sector_buffer[492] = 0x03;
        sector_buffer[493] = 0x00;
        sector_buffer[494] = 0x00;
        sector_buffer[495] = 0x00;

        sector_buffer[510] = 0x55;
        sector_buffer[511] = 0xAA;

        if (block_write_sector(g_partition_offset + 1, sector_buffer) != 0)
        {
                text("FAT32: Failed to write FSInfo\n");
                return -1;
        }
        block_write_sector(g_partition_offset + 7, sector_buffer);

        /* Initialize FATs */
        memset(sector_buffer, 0, 512);
        uint32_t *fat = (uint32_t *)sector_buffer;
        fat[0] = 0x0FFFFFF8;
        fat[1] = 0x0FFFFFFF;
        fat[2] = 0x0FFFFFFF;

        for (uint32_t i = 0; i < num_fats; i++)
        {
                uint32_t fat_start = reserved_sectors + (i * fat_size);

                if (block_write_sector(g_partition_offset + fat_start,
                    sector_buffer) != 0)
                {
                        text("FAT32: Failed to write FAT\n");
                        return -1;
                }

                memset(sector_buffer, 0, 512);
                for (uint32_t j = 1; j < fat_size; j++)
                {
                        if (block_write_sector(
                            g_partition_offset + fat_start + j,
                            sector_buffer) != 0)
                        {
                                text("FAT32: Failed to clear FAT\n");
                                return -1;
                        }
                }
        }

        /* Clear root directory cluster */
        memset(sector_buffer, 0, 512);
        uint32_t root_lba = reserved_sectors + (num_fats * fat_size);
        for (uint32_t i = 0; i < sectors_per_cluster; i++)
        {
                if (block_write_sector(g_partition_offset + root_lba + i,
                    sector_buffer) != 0)
                {
                        text("FAT32: Failed to write root dir\n");
                        return -1;
                }
        }

        g_free_clusters = free_clusters;

        /* Flush and wait for write cache to drain */
        if (sata_is_present())
        {
                for (int _f = 0; _f < 5; _f++)
                {
                        if (sata_flush_cache() != 0)
                                text("FAT32: warning — sata_flush_cache failed during format\n");
                        for (volatile int _d = 0; _d < 100000000; _d++);
                }
        }

        text("FAT32: Format complete\n");
        return 0;
}

/* Initialize FAT32 filesystem */
int
fat32_init(void)
{
        if (!block_is_present())
        {
                text("FAT32: Block device not initialized\n");
                return -1;
        }

        if (block_read_sector(g_partition_offset + 0, sector_buffer) != 0)
        {
                text("FAT32: Failed to read boot sector\n");
                return -1;
        }

        fat32_boot_sector_t *boot = (fat32_boot_sector_t *)sector_buffer;

        if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA)
        {
                text("FAT32: Invalid boot signature\n");
                return -1;
        }

        g_fs.fat_begin_lba      = boot->reserved_sectors;
        g_fs.fat_size           = boot->fat_size_32;
        g_fs.num_fats           = boot->num_fats;
        g_fs.sectors_per_cluster = boot->sectors_per_cluster;
        g_fs.root_dir_cluster   = boot->root_cluster;
        g_fs.cluster_begin_lba  = boot->reserved_sectors +
                                  (boot->num_fats * boot->fat_size_32);

        uint32_t data_sectors   = boot->total_sectors_32 -
                                  g_fs.cluster_begin_lba;
        g_fs.total_clusters     = data_sectors / boot->sectors_per_cluster;
        g_fs.initialized        = 1;

        if (block_read_sector(g_partition_offset + 1, sector_buffer) == 0)
        {
                if (sector_buffer[0] == 0x52 && sector_buffer[1] == 0x52 &&
                    sector_buffer[2] == 0x61 && sector_buffer[3] == 0x41)
                {
                        g_free_clusters =
                                sector_buffer[488] |
                                ((uint32_t)sector_buffer[489] << 8)  |
                                ((uint32_t)sector_buffer[490] << 16) |
                                ((uint32_t)sector_buffer[491] << 24);
                }
                else
                {
                        g_free_clusters = 0xFFFFFFFF;
                }
        }
        else
        {
                g_free_clusters = 0xFFFFFFFF;
        }

        text("FAT32: Filesystem initialized\n");
        return 0;
}

/* Read file from root directory */
int
fat32_read_file(const char *filename, void *buffer,
                uint32_t max_size, uint32_t *bytes_read)
{
        return fat32_read_file_in(g_fs.root_dir_cluster, filename,
                                  buffer, max_size, bytes_read);
}

/* Read file from a specific directory cluster */
int
fat32_read_file_in(uint32_t parent_cluster, const char *filename,
                   void *buffer, uint32_t max_size, uint32_t *bytes_read)
{
        if (!g_fs.initialized)
        {
                text("FAT32: Not initialized\n");
                return -1;
        }

        uint8_t fat_name[11];
        name_to_fat(filename, fat_name);

        fat32_dir_entry_t entry;
        if (fat32_find_entry(parent_cluster, fat_name, &entry, 0, 0) != 0)
        {
                text("FAT32: File not found\n");
                return -1;
        }

        uint32_t first_cluster = ((uint32_t)entry.first_cluster_hi << 16) |
                                  entry.first_cluster_lo;
        uint32_t file_size = entry.file_size;
        uint32_t to_read   = (file_size < max_size) ? file_size : max_size;

        static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));
        uint32_t cluster        = first_cluster;
        uint32_t bytes_remaining = to_read;
        uint8_t *dest           = buffer;

        while (cluster >= 2 && cluster < FAT32_EOC && bytes_remaining > 0)
        {
                if (fat32_read_cluster(cluster, cluster_buffer) != 0)
                {
                        text("FAT32: Failed to read cluster\n");
                        return -1;
                }

                uint32_t chunk = (bytes_remaining <
                                  (g_fs.sectors_per_cluster * 512)) ?
                                  bytes_remaining :
                                  (g_fs.sectors_per_cluster * 512);

                memcpy(dest, cluster_buffer, chunk);
                dest            += chunk;
                bytes_remaining -= chunk;

                cluster = fat32_get_next_cluster(cluster);
        }

        if (bytes_read)
                *bytes_read = to_read;
        return 0;
}

/* Compare file contents with expected bytes */
int
fat32_compare_file_in(uint32_t parent_cluster, const char *filename,
                      const void *expected, uint32_t expected_size,
                      uint32_t *mismatch_offset,
                      uint8_t *disk_snip, uint8_t *expected_snip,
                      uint32_t snip_len)
{
        if (!g_fs.initialized)
        {
                text("FAT32: Not initialized\n");
                return -1;
        }

        uint8_t fat_name[11];
        name_to_fat(filename, fat_name);

        fat32_dir_entry_t entry;
        if (fat32_find_entry(parent_cluster, fat_name, &entry, 0, 0) != 0)
        {
                text("FAT32: File not found\n");
                return -1;
        }

        uint32_t file_size = entry.file_size;
        if (file_size != expected_size)
        {
                if (mismatch_offset)
                        *mismatch_offset = (file_size < expected_size) ?
                                           file_size : expected_size;
                return -1;
        }

        uint32_t first_cluster = ((uint32_t)entry.first_cluster_hi << 16) |
                                  entry.first_cluster_lo;
        static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));
        uint32_t cluster  = first_cluster;
        uint32_t file_pos = 0;
        const uint8_t *exp = expected;

        while (cluster >= 2 && cluster < FAT32_EOC && file_pos < file_size)
        {
                if (fat32_read_cluster(cluster, cluster_buffer) != 0)
                {
                        text("FAT32: Failed to read cluster\n");
                        return -1;
                }

                uint32_t chunk = (file_size - file_pos <
                                  (g_fs.sectors_per_cluster * 512)) ?
                                  (file_size - file_pos) :
                                  (g_fs.sectors_per_cluster * 512);

                for (uint32_t i = 0; i < chunk; i++)
                {
                        if (cluster_buffer[i] != exp[file_pos + i])
                        {
                                if (mismatch_offset)
                                        *mismatch_offset = file_pos + i;
                                if (snip_len && disk_snip && expected_snip)
                                {
                                        uint32_t remain = file_size -
                                                          (file_pos + i);
                                        uint32_t copy_len = (snip_len < remain) ?
                                                             snip_len : remain;
                                        uint32_t copied = 0;
                                        uint32_t off    = i;
                                        uint32_t cur    = cluster;
                                        while (copied < copy_len && cur >= 2 &&
                                               cur < FAT32_EOC)
                                        {
                                                uint32_t avail =
                                                        (g_fs.sectors_per_cluster * 512) - off;
                                                uint32_t to_copy =
                                                        (avail < copy_len - copied) ?
                                                        avail : copy_len - copied;
                                                memcpy(disk_snip + copied,
                                                       cluster_buffer + off,
                                                       to_copy);
                                                memcpy(expected_snip + copied,
                                                       exp + file_pos + i + copied,
                                                       to_copy);
                                                copied += to_copy;
                                                off     = 0;
                                                cur = fat32_get_next_cluster(cur);
                                                if (copied < copy_len &&
                                                    cur >= 2 && cur < FAT32_EOC)
                                                {
                                                        if (fat32_read_cluster(
                                                            cur, cluster_buffer) != 0)
                                                                break;
                                                }
                                        }
                                        for (uint32_t z = copied; z < copy_len; z++)
                                        {
                                                disk_snip[z]     = 0;
                                                expected_snip[z] = 0;
                                        }
                                }
                                return -1;
                        }
                }

                file_pos += chunk;
                cluster   = fat32_get_next_cluster(cluster);
        }

        return 0;
}

/* Get file size for a file in a given directory cluster
 * FIX: walk full cluster chain */
int
fat32_get_file_size_in(uint32_t parent_cluster, const char *filename,
                       uint32_t *size)
{
        if (!g_fs.initialized)
        {
                text("FAT32: Not initialized\n");
                return -1;
        }

        uint8_t fat_name[11];
        name_to_fat(filename, fat_name);

        fat32_dir_entry_t entry;
        if (fat32_find_entry(parent_cluster, fat_name, &entry, 0, 0) != 0)
        {
                text("FAT32: File not found\n");
                return -1;
        }

        if (size)
                *size = entry.file_size;
        return 0;
}

/* Write file to root */
int
fat32_write_file(const char *filename, const void *buffer, uint32_t size)
{
        return fat32_write_file_in(g_fs.root_dir_cluster, filename,
                                   buffer, size);
}

/* Write file to a given directory cluster
 * FIX: walks full cluster chain for directory scan; flushes after all writes. */
int
fat32_write_file_in(uint32_t parent_cluster, const char *filename,
                    const void *buffer, uint32_t size)
{
        if (!g_fs.initialized)
        {
                text("FAT32: Not initialized\n");
                return -1;
        }

        uint8_t fat_name[11];
        name_to_fat(filename, fat_name);

        /* Walk the full directory cluster chain to find a free slot or
         * an existing entry with the same name. */
        static uint8_t dir_buffer[4096] __attribute__((aligned(16)));

        uint32_t entry_cluster = 0; /* cluster that holds the chosen slot */
        uint32_t entry_index   = 0;
        fat32_dir_entry_t *entry_ptr = NULL;

        /* We keep a copy of the dir cluster so we can write it back. */
        static uint8_t write_dir_buffer[4096] __attribute__((aligned(16)));

        uint32_t cur = parent_cluster;
        while (cur >= 2 && cur < FAT32_EOC)
        {
                if (fat32_read_cluster(cur, dir_buffer) != 0)
                {
                        text("FAT32: Failed to read directory cluster\n");
                        return -1;
                }

                fat32_dir_entry_t *entries = (fat32_dir_entry_t *)dir_buffer;
                uint32_t epc = (g_fs.sectors_per_cluster * 512) /
                               sizeof(fat32_dir_entry_t);

                for (uint32_t i = 0; i < epc; i++)
                {
                        if (entries[i].name[0] == 0x00 ||
                            entries[i].name[0] == 0xE5)
                        {
                                /* Free slot — use it if we haven't found one yet */
                                if (entry_cluster == 0)
                                {
                                        entry_cluster = cur;
                                        entry_index   = i;
                                        memcpy(write_dir_buffer, dir_buffer,
                                               g_fs.sectors_per_cluster * 512);
                                }
                                if (entries[i].name[0] == 0x00)
                                        goto scan_done; /* end of directory */
                        }
                        else if (memcmp(entries[i].name, fat_name, 11) == 0)
                        {
                                /* Existing file — free its clusters, reuse slot */
                                entry_cluster = cur;
                                entry_index   = i;
                                memcpy(write_dir_buffer, dir_buffer,
                                       g_fs.sectors_per_cluster * 512);

                                uint32_t old_cluster =
                                        ((uint32_t)entries[i].first_cluster_hi << 16) |
                                        entries[i].first_cluster_lo;
                                while (old_cluster >= 2 && old_cluster < FAT32_EOC)
                                {
                                        uint32_t nxt =
                                                fat32_get_next_cluster(old_cluster);
                                        fat32_free_cluster(old_cluster);
                                        old_cluster = nxt;
                                }
                                goto scan_done;
                        }
                }

                cur = fat32_get_next_cluster(cur);
        }

scan_done:
        if (entry_cluster == 0)
        {
                text("FAT32: Directory full\n");
                return -1;
        }

        /* Allocate clusters and write file data */
        static uint8_t file_cluster_buffer[4096] __attribute__((aligned(16)));
        static uint8_t verify_buffer[4096]        __attribute__((aligned(16)));

        uint32_t bytes_remaining = size;
        const uint8_t *src       = buffer;
        uint32_t first_cluster   = 0;
        uint32_t prev_cluster    = 0;

        while (bytes_remaining > 0)
        {
                uint32_t cluster = fat32_allocate_cluster();
                if (cluster == 0 || cluster < 2)
                {
                        text("FAT32: Disk full or allocation error\n");
                        return -1;
                }

                if (first_cluster == 0)
                        first_cluster = cluster;

                if (prev_cluster != 0)
                        fat32_set_next_cluster(prev_cluster, cluster);

                uint32_t chunk = (bytes_remaining <
                                  (g_fs.sectors_per_cluster * 512)) ?
                                  bytes_remaining :
                                  (g_fs.sectors_per_cluster * 512);

                memset(file_cluster_buffer, 0,
                       g_fs.sectors_per_cluster * 512);
                memcpy(file_cluster_buffer, src, chunk);

                if (fat32_write_cluster(cluster, file_cluster_buffer) != 0)
                {
                        text("FAT32: Failed to write cluster\n");
                        return -1;
                }

                /* Read-back verify */
                if (fat32_read_cluster(cluster, verify_buffer) != 0)
                {
                        text("FAT32: Read-back failed on cluster\n");
                        return -1;
                }
                if (memcmp(verify_buffer, file_cluster_buffer,
                           g_fs.sectors_per_cluster * 512) != 0)
                {
                        text("FAT32: Write verification failed on cluster\n");
                        return -1;
                }

                src             += chunk;
                bytes_remaining -= chunk;
                prev_cluster     = cluster;
        }

        /* Update directory entry in our cached copy */
        fat32_dir_entry_t *entries =
                (fat32_dir_entry_t *)write_dir_buffer;
        fat32_dir_entry_t *entry = &entries[entry_index];

        memset(entry, 0, sizeof(fat32_dir_entry_t));
        memcpy(entry->name, fat_name, 11);
        entry->attr             = ATTR_ARCHIVE;
        entry->nt_reserved      = 0;
        entry->create_time_tenth = 0;
        entry->create_time      = 0;
        entry->create_date      = 0;
        entry->last_access_date = 0;
        entry->first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
        entry->write_time       = 0;
        entry->write_date       = 0;
        entry->first_cluster_lo = first_cluster & 0xFFFF;
        entry->file_size        = size;

        /* Write the directory cluster back */
        if (fat32_write_cluster(entry_cluster, write_dir_buffer) != 0)
        {
                text("FAT32: Failed to update directory\n");
                return -1;
        }

        if (g_free_clusters != 0xFFFFFFFF)
                fat32_update_fsinfo(g_free_clusters, prev_cluster + 1);

        /* Flush SATA write cache so data actually hits the disk */
        if (sata_is_present())
        {
                for (int _f = 0; _f < 5; _f++)
                {
                        if (sata_flush_cache() != 0)
                                text("FAT32: warning — sata_flush_cache failed after write\n");
                        for (volatile int _d = 0; _d < 100000000; _d++);
                }
        }

        return 0;
}

/* -------------------------------------------------------------------------
 * fat32_create_directory_in
 * FIX: walks full cluster chain for free-slot scan; flushes after write.
 * ------------------------------------------------------------------------- */
int
fat32_create_directory_in(uint32_t parent_cluster, const char *dirname)
{
        if (!g_fs.initialized)
        {
                text("FAT32: Not initialized\n");
                return -1;
        }

        uint8_t fat_name[11];
        name_to_fat(dirname, fat_name);

        static uint8_t dir_buffer[4096] __attribute__((aligned(16)));
        static uint8_t write_dir_buffer[4096] __attribute__((aligned(16)));

        uint32_t entry_cluster = 0;
        uint32_t entry_index   = 0;

        uint32_t cur = parent_cluster;
        while (cur >= 2 && cur < FAT32_EOC)
        {
                if (fat32_read_cluster(cur, dir_buffer) != 0)
                {
                        text("FAT32: Failed to read directory cluster\n");
                        return -1;
                }

                fat32_dir_entry_t *entries = (fat32_dir_entry_t *)dir_buffer;
                uint32_t epc = (g_fs.sectors_per_cluster * 512) /
                               sizeof(fat32_dir_entry_t);

                for (uint32_t i = 0; i < epc; i++)
                {
                        /* Skip . and .. in non-root dirs */
                        if (cur != g_fs.root_dir_cluster)
                        {
                                if (i == 0 &&
                                    memcmp(entries[i].name, ".          ", 11) == 0)
                                        continue;
                                if (i == 1 &&
                                    memcmp(entries[i].name, "..         ", 11) == 0)
                                        continue;
                        }

                        if (entries[i].name[0] == 0x00 ||
                            entries[i].name[0] == 0xE5)
                        {
                                if (entry_cluster == 0)
                                {
                                        entry_cluster = cur;
                                        entry_index   = i;
                                        memcpy(write_dir_buffer, dir_buffer,
                                               g_fs.sectors_per_cluster * 512);
                                }
                                if (entries[i].name[0] == 0x00)
                                        goto dir_scan_done;
                        }
                        else if (memcmp(entries[i].name, fat_name, 11) == 0)
                        {
                                text("FAT32: Directory already exists\n");
                                return -1;
                        }
                }

                cur = fat32_get_next_cluster(cur);
        }

dir_scan_done:
        if (entry_cluster == 0)
        {
                text("FAT32: Directory full\n");
                return -1;
        }

        /* Allocate cluster for new directory */
        uint32_t dir_cluster = fat32_allocate_cluster();
        if (dir_cluster == 0 || dir_cluster < 2)
        {
                text("FAT32: Failed to allocate cluster for directory\n");
                return -1;
        }

        /* Write . and .. entries */
        static uint8_t cluster_buf[4096] __attribute__((aligned(16)));
        memset(cluster_buf, 0, g_fs.sectors_per_cluster * 512);

        fat32_dir_entry_t *dot_entries = (fat32_dir_entry_t *)cluster_buf;

        memcpy(dot_entries[0].name, ".          ", 11);
        dot_entries[0].attr             = ATTR_DIRECTORY;
        dot_entries[0].first_cluster_hi = (dir_cluster >> 16) & 0xFFFF;
        dot_entries[0].first_cluster_lo = dir_cluster & 0xFFFF;
        dot_entries[0].file_size        = 0;

        /* .. points to 0 when parent is root (FAT32 spec requirement) */
        uint32_t dotdot_cluster =
                (parent_cluster == g_fs.root_dir_cluster) ? 0 : parent_cluster;
        memcpy(dot_entries[1].name, "..         ", 11);
        dot_entries[1].attr             = ATTR_DIRECTORY;
        dot_entries[1].first_cluster_hi = (dotdot_cluster >> 16) & 0xFFFF;
        dot_entries[1].first_cluster_lo = dotdot_cluster & 0xFFFF;
        dot_entries[1].file_size        = 0;

        if (fat32_write_cluster(dir_cluster, cluster_buf) != 0)
        {
                text("FAT32: Failed to write new directory cluster\n");
                fat32_free_cluster(dir_cluster);
                return -1;
        }

        /* Update parent directory entry */
        fat32_dir_entry_t *entries    = (fat32_dir_entry_t *)write_dir_buffer;
        fat32_dir_entry_t *slot       = &entries[entry_index];

        memset(slot, 0, sizeof(fat32_dir_entry_t));
        memcpy(slot->name, fat_name, 11);
        slot->attr             = ATTR_DIRECTORY;
        slot->first_cluster_hi = (dir_cluster >> 16) & 0xFFFF;
        slot->first_cluster_lo = dir_cluster & 0xFFFF;
        slot->file_size        = 0;

        if (fat32_write_cluster(entry_cluster, write_dir_buffer) != 0)
        {
                text("FAT32: Failed to update parent directory\n");
                return -1;
        }

        if (g_free_clusters != 0xFFFFFFFF)
                fat32_update_fsinfo(g_free_clusters, dir_cluster + 1);

        /* Flush SATA write cache */
        if (sata_is_present())
        {
                for (int _f = 0; _f < 5; _f++)
                {
                        if (sata_flush_cache() != 0)
                                text("FAT32: warning — sata_flush_cache failed after mkdir\n");
                        for (volatile int _d = 0; _d < 100000000; _d++);
                }
        }

        return 0;
}

/* Create directory in root */
int
fat32_create_directory(const char *dirname)
{
        return fat32_create_directory_in(g_fs.root_dir_cluster, dirname);
}

/* Get root directory cluster */
uint32_t
fat32_get_root_cluster(void)
{
        return g_fs.root_dir_cluster;
}

/* Find subdirectory cluster */
uint32_t
fat32_find_directory(uint32_t parent_cluster, const char *dirname)
{
        uint8_t fat_name[11];
        name_to_fat(dirname, fat_name);

        fat32_dir_entry_t entry;
        if (fat32_find_entry(parent_cluster, fat_name, &entry, 0, 0) != 0)
                return 0;

        if (!(entry.attr & ATTR_DIRECTORY))
                return 0;

        return ((uint32_t)entry.first_cluster_hi << 16) |
               entry.first_cluster_lo;
}

/* Check if directory is empty */
static int
fat32_is_directory_empty(uint32_t dir_cluster)
{
        static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));

        uint32_t cur = dir_cluster;
        while (cur >= 2 && cur < FAT32_EOC)
        {
                if (fat32_read_cluster(cur, cluster_buffer) != 0)
                        return -1;

                fat32_dir_entry_t *entries =
                        (fat32_dir_entry_t *)cluster_buffer;
                uint32_t epc = (g_fs.sectors_per_cluster * 512) /
                               sizeof(fat32_dir_entry_t);

                for (uint32_t i = 0; i < epc; i++)
                {
                        if (entries[i].name[0] == 0x00)
                                return 1;
                        if (entries[i].name[0] == 0xE5)
                                continue;
                        if (memcmp(entries[i].name, ".          ", 11) == 0)
                                continue;
                        if (memcmp(entries[i].name, "..         ", 11) == 0)
                                continue;
                        return 0; /* found a real entry */
                }

                cur = fat32_get_next_cluster(cur);
        }

        return 1;
}

/* Delete directory
 * FIX: walks full cluster chain to find the entry. */
int
fat32_delete_directory_in(uint32_t parent_cluster, const char *dirname)
{
        if (!g_fs.initialized)
                return -1;

        uint8_t fat_name[11];
        name_to_fat(dirname, fat_name);

        fat32_dir_entry_t entry;
        uint32_t found_cluster = 0;
        uint32_t found_index   = 0;

        if (fat32_find_entry(parent_cluster, fat_name, &entry,
                             &found_cluster, &found_index) != 0)
        {
                text("FAT32: Directory not found\n");
                return -1;
        }

        if (!(entry.attr & ATTR_DIRECTORY))
        {
                text("FAT32: Not a directory\n");
                return -1;
        }

        uint32_t dir_cluster = ((uint32_t)entry.first_cluster_hi << 16) |
                               entry.first_cluster_lo;

        if (!fat32_is_directory_empty(dir_cluster))
        {
                text("FAT32: Directory not empty\n");
                return -1;
        }

        fat32_free_cluster(dir_cluster);

        /* Re-read the cluster that holds the entry and mark it deleted */
        static uint8_t dir_buffer[4096] __attribute__((aligned(16)));
        if (fat32_read_cluster(found_cluster, dir_buffer) != 0)
                return -1;

        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)dir_buffer;
        entries[found_index].name[0] = 0xE5;

        if (fat32_write_cluster(found_cluster, dir_buffer) != 0)
                return -1;

        if (g_free_clusters != 0xFFFFFFFF)
                fat32_update_fsinfo(g_free_clusters, 0xFFFFFFFF);

        return 0;
}

int
fat32_delete_directory(const char *dirname)
{
        return fat32_delete_directory_in(g_fs.root_dir_cluster, dirname);
}

/* Delete file from FAT32
 * FIX: walks full cluster chain. */
int
fat32_delete_file(const char *filename)
{
        if (!g_fs.initialized)
                return -1;

        uint8_t fat_name[11];
        name_to_fat(filename, fat_name);

        fat32_dir_entry_t entry;
        uint32_t found_cluster = 0;
        uint32_t found_index   = 0;

        if (fat32_find_entry(g_fs.root_dir_cluster, fat_name, &entry,
                             &found_cluster, &found_index) != 0)
                return -1;

        /* Free data clusters */
        uint32_t cluster = ((uint32_t)entry.first_cluster_hi << 16) |
                           entry.first_cluster_lo;
        while (cluster >= 2 && cluster < FAT32_EOC)
        {
                uint32_t nxt = fat32_get_next_cluster(cluster);
                fat32_free_cluster(cluster);
                cluster = nxt;
        }

        /* Mark entry deleted */
        static uint8_t dir_buffer[4096] __attribute__((aligned(16)));
        if (fat32_read_cluster(found_cluster, dir_buffer) != 0)
                return -1;

        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)dir_buffer;
        entries[found_index].name[0] = 0xE5;

        if (fat32_write_cluster(found_cluster, dir_buffer) != 0)
                return -1;

        if (g_free_clusters != 0xFFFFFFFF)
                fat32_update_fsinfo(g_free_clusters, 0xFFFFFFFF);

        return 0;
}

/* List files in root directory
 * FIX: walks full cluster chain. */
int
fat32_list_files(void (*callback)(const char *name, uint32_t size,
                                   uint8_t is_dir))
{
        if (!g_fs.initialized || !callback)
                return -1;

        static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));
        static char filename[13];

        uint32_t cur = g_fs.root_dir_cluster;
        while (cur >= 2 && cur < FAT32_EOC)
        {
                if (fat32_read_cluster(cur, cluster_buffer) != 0)
                        return -1;

                fat32_dir_entry_t *entries =
                        (fat32_dir_entry_t *)cluster_buffer;
                uint32_t epc = (g_fs.sectors_per_cluster * 512) /
                               sizeof(fat32_dir_entry_t);

                for (uint32_t i = 0; i < epc; i++)
                {
                        if (entries[i].name[0] == 0x00)
                                return 0;
                        if (entries[i].name[0] == 0xE5)
                                continue;
                        if (entries[i].attr & ATTR_VOLUME_ID)
                                continue;

                        int j = 0, k = 0;
                        while (j < 8 && entries[i].name[j] != ' ')
                                filename[k++] = entries[i].name[j++];

                        if (entries[i].name[8] != ' ')
                        {
                                filename[k++] = '.';
                                j = 8;
                                while (j < 11 && entries[i].name[j] != ' ')
                                        filename[k++] = entries[i].name[j++];
                        }
                        filename[k] = '\0';

                        callback(filename, entries[i].file_size,
                                 entries[i].attr & ATTR_DIRECTORY);
                }

                cur = fat32_get_next_cluster(cur);
        }

        return 0;
}