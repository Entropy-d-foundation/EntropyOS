/* SPDX-License-Identifier: GPL-2.0 */
#include "../../drivers/block/block.h"
#include "gpt.h"
#include <stdint.h>

/* Global GPT state */
static gpt_header_t g_gpt_header;
static gpt_partition_entry_t g_partitions[GPT_MAX_PARTITIONS];
static uint8_t g_gpt_initialized = 0;

/* Sector buffer for I/O */
static uint8_t sector_buffer[512] __attribute__((aligned(16)));

/* Memory operations */
static void
memcpy(void *dest, const void *src, uint32_t n)
{
        uint8_t *d = dest;
        const uint8_t *s = src;
        for (uint32_t i = 0; i < n; i++)
                d[i] = s[i];
}

static void
memset(void *s, int c, uint32_t n)
{
        uint8_t *p = s;
        for (uint32_t i = 0; i < n; i++)
                p[i] = (uint8_t)c;
}

static int
memcmp(const void *s1, const void *s2, uint32_t n)
{
        const uint8_t *p1 = s1, *p2 = s2;
        for (uint32_t i = 0; i < n; i++) {
                if (p1[i] != p2[i])
                        return p1[i] - p2[i];
        }
        return 0;
}

static uint32_t
strlen(const char *s)
{
        uint32_t len = 0;
        while (s[len]) len++;
        return len;
}

/* CRC32 lookup table */
static const uint32_t crc32_table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
        0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
        0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
        0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
        0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
        0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
        0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
        0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
        0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
        0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
        0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
        0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
        0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
        0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
        0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
        0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
        0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
        0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
        0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
        0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
        0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
        0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

/* Calculate CRC32 */
static uint32_t
crc32(const void *data, uint32_t length)
{
        const uint8_t *p = data;
        uint32_t crc = 0xFFFFFFFF;
        
        for (uint32_t i = 0; i < length; i++)
                crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        
        return crc ^ 0xFFFFFFFF;
}

/* Generate a simple pseudo-random GUID */
static void
generate_guid(uint8_t guid[16])
{
        static uint32_t seed = 0x12345678;
        
        for (int i = 0; i < 16; i++) {
                seed = seed * 1103515245 + 12345;
                guid[i] = (seed >> 16) & 0xFF;
        }
        
        /* Set version (4) and variant (RFC 4122) bits */
        guid[6] = (guid[6] & 0x0F) | 0x40;
        guid[8] = (guid[8] & 0x3F) | 0x80;
}

/* Convert UTF-8 string to UTF-16LE */
static void
utf8_to_utf16le(const char *utf8, uint16_t *utf16, uint32_t max_chars)
{
        uint32_t i = 0;
        while (*utf8 && i < max_chars - 1) {
                utf16[i++] = (uint16_t)*utf8++;
        }
        utf16[i] = 0;
}

/* Convert UTF-16LE to UTF-8 */
static void
utf16le_to_utf8(const uint16_t *utf16, char *utf8, uint32_t max_bytes)
{
        uint32_t i = 0;
        while (*utf16 && i < max_bytes - 1) {
                utf8[i++] = (char)*utf16++;
        }
        utf8[i] = '\0';
}

/* Initialize GPT from disk */
int
gpt_init(void)
{
if (!block_is_present())
		return -1;
	
	if (block_read_sector(1, sector_buffer) != 0)
                return -1;
        
        memcpy(&g_gpt_header, sector_buffer, sizeof(gpt_header_t));
        
        if (memcmp(g_gpt_header.signature, GPT_SIGNATURE, 8) != 0)
                return -1;
        
        if (g_gpt_header.revision != GPT_REVISION)
                return -1;
        
        uint32_t saved_crc = g_gpt_header.header_crc32;
        g_gpt_header.header_crc32 = 0;
        uint32_t calc_crc = crc32(&g_gpt_header, g_gpt_header.header_size);
        
        if (saved_crc != calc_crc)
                return -1;
        
        g_gpt_header.header_crc32 = saved_crc;
        
        uint32_t entries_to_read = g_gpt_header.num_partition_entries;
        if (entries_to_read > GPT_MAX_PARTITIONS)
                entries_to_read = GPT_MAX_PARTITIONS;
        
        uint32_t sectors_needed = 
                (entries_to_read * g_gpt_header.partition_entry_size + 511) / 512;
        
        for (uint32_t i = 0; i < sectors_needed; i++) {
                if (block_read_sector(g_gpt_header.partition_entries_lba + i,
                                   sector_buffer) != 0)
                        return -1;
                
                uint32_t entries_this_sector = 512 / g_gpt_header.partition_entry_size;
                uint32_t base_entry = i * entries_this_sector;
                
                for (uint32_t j = 0; j < entries_this_sector; j++) {
                        uint32_t entry_idx = base_entry + j;
                        if (entry_idx >= entries_to_read)
                                break;
                        
                        memcpy(&g_partitions[entry_idx],
                              sector_buffer + (j * g_gpt_header.partition_entry_size),
                              sizeof(gpt_partition_entry_t));
                }
        }
        
        g_gpt_initialized = 1;
        return 0;
}

/* Create a new GPT partition table */
int
gpt_create_table(uint64_t total_sectors, const uint8_t disk_guid[16])
{
        if (!block_is_present())
                return -1;
        
        protective_mbr_t *mbr = (protective_mbr_t *)sector_buffer;
        memset(mbr, 0, 512);
        
        mbr->disk_signature = 0;
        mbr->partitions[0].status = 0x00;
        mbr->partitions[0].type = 0xEE;
        mbr->partitions[0].first_lba = 1;
        mbr->partitions[0].num_sectors = 
                (total_sectors > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)total_sectors - 1;
        mbr->signature = 0xAA55;
        
        if (block_write_sector(0, sector_buffer) != 0)
                return -1;
        
        memset(&g_gpt_header, 0, sizeof(gpt_header_t));
        memcpy(g_gpt_header.signature, GPT_SIGNATURE, 8);
        g_gpt_header.revision = GPT_REVISION;
        g_gpt_header.header_size = 92;
        g_gpt_header.current_lba = 1;
        g_gpt_header.backup_lba = total_sectors - 1;
        g_gpt_header.first_usable_lba = 34;
        g_gpt_header.last_usable_lba = total_sectors - 34;
        
        if (disk_guid)
                memcpy(g_gpt_header.disk_guid, disk_guid, 16);
        else
                generate_guid(g_gpt_header.disk_guid);
        
        g_gpt_header.partition_entries_lba = 2;
        g_gpt_header.num_partition_entries = GPT_MAX_PARTITIONS;
        g_gpt_header.partition_entry_size = 128;
        
        memset(g_partitions, 0, sizeof(g_partitions));
        
        g_gpt_header.partition_array_crc32 = 
                crc32(g_partitions, GPT_MAX_PARTITIONS * 128);
        
        g_gpt_header.header_crc32 = 0;
        g_gpt_header.header_crc32 = crc32(&g_gpt_header, 92);
        
        memset(sector_buffer, 0, 512);
        memcpy(sector_buffer, &g_gpt_header, sizeof(gpt_header_t));
        
        if (block_write_sector(1, sector_buffer) != 0)
                return -1;
        
        uint32_t sectors_needed = (GPT_MAX_PARTITIONS * 128 + 511) / 512;
        for (uint32_t i = 0; i < sectors_needed; i++) {
                memset(sector_buffer, 0, 512);
                memcpy(sector_buffer, 
                      (uint8_t *)g_partitions + (i * 512),
                      512);
                
                if (block_write_sector(2 + i, sector_buffer) != 0)
                        return -1;
        }
        
        g_gpt_header.current_lba = total_sectors - 1;
        g_gpt_header.backup_lba = 1;
        g_gpt_header.partition_entries_lba = total_sectors - 33;
        g_gpt_header.header_crc32 = 0;
        g_gpt_header.header_crc32 = crc32(&g_gpt_header, 92);
        
        memset(sector_buffer, 0, 512);
        memcpy(sector_buffer, &g_gpt_header, sizeof(gpt_header_t));
        block_write_sector(total_sectors - 1, sector_buffer);
        
        g_gpt_header.current_lba = 1;
        g_gpt_header.backup_lba = total_sectors - 1;
        g_gpt_header.partition_entries_lba = 2;
        
        g_gpt_initialized = 1;
        return 0;
}

/* Add a partition */
int
gpt_add_partition(uint64_t first_lba, uint64_t last_lba,
                  const uint8_t type_guid[16], const char *name)
{
        if (!g_gpt_initialized)
                return -1;
        
        int free_idx = -1;
        for (uint32_t i = 0; i < GPT_MAX_PARTITIONS; i++) {
                uint8_t zero_guid[16] = {0};
                if (memcmp(g_partitions[i].type_guid, zero_guid, 16) == 0) {
                        free_idx = i;
                        break;
                }
        }
        
        if (free_idx < 0)
                return -1;
        
        if (first_lba < g_gpt_header.first_usable_lba ||
            last_lba > g_gpt_header.last_usable_lba ||
            first_lba >= last_lba)
                return -1;
        
        memcpy(g_partitions[free_idx].type_guid, type_guid, 16);
        generate_guid(g_partitions[free_idx].partition_guid);
        g_partitions[free_idx].first_lba = first_lba;
        g_partitions[free_idx].last_lba = last_lba;
        g_partitions[free_idx].attributes = 0;
        
        if (name)
                utf8_to_utf16le(name, g_partitions[free_idx].name, 36);
        
        g_gpt_header.partition_array_crc32 = 
                crc32(g_partitions, GPT_MAX_PARTITIONS * 128);
        
        g_gpt_header.header_crc32 = 0;
        g_gpt_header.header_crc32 = crc32(&g_gpt_header, 92);
        
        memset(sector_buffer, 0, 512);
        memcpy(sector_buffer, &g_gpt_header, sizeof(gpt_header_t));
        
        if (block_write_sector(1, sector_buffer) != 0)
                return -1;
        
        uint32_t sectors_needed = (GPT_MAX_PARTITIONS * 128 + 511) / 512;
        for (uint32_t i = 0; i < sectors_needed; i++) {
                memset(sector_buffer, 0, 512);
                memcpy(sector_buffer, (uint8_t *)g_partitions + (i * 512), 512);
                
                if (block_write_sector(2 + i, sector_buffer) != 0)
                        return -1;
        }
        
        return 0;
}

/* Get partition information */
int
gpt_get_partition(uint32_t index, gpt_partition_info_t *info)
{
        if (!g_gpt_initialized || index >= GPT_MAX_PARTITIONS || !info)
                return -1;
        
        uint8_t zero_guid[16] = {0};
        if (memcmp(g_partitions[index].type_guid, zero_guid, 16) == 0) {
                info->valid = 0;
                return -1;
        }
        
        memcpy(info->type_guid, g_partitions[index].type_guid, 16);
        memcpy(info->partition_guid, g_partitions[index].partition_guid, 16);
        info->first_lba = g_partitions[index].first_lba;
        info->last_lba = g_partitions[index].last_lba;
        info->attributes = g_partitions[index].attributes;
        utf16le_to_utf8(g_partitions[index].name, info->name, 73);
        info->valid = 1;
        
        return 0;
}

/* List all partitions */
int
gpt_list_partitions(void (*callback)(uint32_t index,
                                     const gpt_partition_info_t *info))
{
        if (!g_gpt_initialized || !callback)
                return -1;
        
        gpt_partition_info_t info;
        for (uint32_t i = 0; i < GPT_MAX_PARTITIONS; i++) {
                if (gpt_get_partition(i, &info) == 0)
                        callback(i, &info);
        }
        
        return 0;
}

/* Delete partition */
int
gpt_delete_partition(uint32_t index)
{
        if (!g_gpt_initialized || index >= GPT_MAX_PARTITIONS)
                return -1;
        
        memset(&g_partitions[index], 0, sizeof(gpt_partition_entry_t));
        
        g_gpt_header.partition_array_crc32 = 
                crc32(g_partitions, GPT_MAX_PARTITIONS * 128);
        g_gpt_header.header_crc32 = 0;
        g_gpt_header.header_crc32 = crc32(&g_gpt_header, 92);
        
        memset(sector_buffer, 0, 512);
        memcpy(sector_buffer, &g_gpt_header, sizeof(gpt_header_t));
        
        if (block_write_sector(1, sector_buffer) != 0)
                return -1;
        
        uint32_t sectors_needed = (GPT_MAX_PARTITIONS * 128 + 511) / 512;
        for (uint32_t i = 0; i < sectors_needed; i++) {
                memset(sector_buffer, 0, 512);
                memcpy(sector_buffer, (uint8_t *)g_partitions + (i * 512), 512);
                
                if (block_write_sector(2 + i, sector_buffer) != 0)
                        return -1;
        }
        
        return 0;
}

/* Verify GPT integrity */
int
gpt_verify(void)
{
        if (!g_gpt_initialized)
                return -1;
        
        uint32_t saved_crc = g_gpt_header.header_crc32;
        g_gpt_header.header_crc32 = 0;
        uint32_t calc_crc = crc32(&g_gpt_header, g_gpt_header.header_size);
        g_gpt_header.header_crc32 = saved_crc;
        
        if (saved_crc != calc_crc)
                return -1;
        
        uint32_t array_crc = crc32(g_partitions, GPT_MAX_PARTITIONS * 128);
        if (array_crc != g_gpt_header.partition_array_crc32)
                return -1;
        
        return 0;
}