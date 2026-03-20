/* SPDX-License-Identifier: GPL-3.0 */
#ifndef GPT_H
#define GPT_H

#include <stdint.h>

/* GPT Partition Type GUIDs */
#define GPT_TYPE_EFI_SYSTEM \
        {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, \
         0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}

#define GPT_TYPE_MICROSOFT_BASIC_DATA \
        {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, \
         0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}

#define GPT_TYPE_LINUX_FILESYSTEM \
        {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, \
         0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}

#define GPT_TYPE_LINUX_SWAP \
        {0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43, \
         0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F}

/* GPT Signature */
#define GPT_SIGNATURE "EFI PART"
#define GPT_REVISION 0x00010000

/* Maximum number of partition entries we support */
#define GPT_MAX_PARTITIONS 128

/* GPT Header structure (LBA 1) */
typedef struct __attribute__((packed)) {
        uint8_t signature[8];           /* "EFI PART" */
        uint32_t revision;              /* GPT revision */
        uint32_t header_size;           /* Header size in bytes */
        uint32_t header_crc32;          /* CRC32 of header */
        uint32_t reserved;              /* Must be zero */
        uint64_t current_lba;           /* LBA of this header */
        uint64_t backup_lba;            /* LBA of backup header */
        uint64_t first_usable_lba;      /* First usable LBA */
        uint64_t last_usable_lba;       /* Last usable LBA */
        uint8_t disk_guid[16];          /* Unique disk GUID */
        uint64_t partition_entries_lba; /* LBA of partition array */
        uint32_t num_partition_entries; /* Number of entries */
        uint32_t partition_entry_size;  /* Size of each entry */
        uint32_t partition_array_crc32; /* CRC32 of array */
} gpt_header_t;

/* GPT Partition Entry structure */
typedef struct __attribute__((packed)) {
        uint8_t type_guid[16];          /* Partition type GUID */
        uint8_t partition_guid[16];     /* Unique partition GUID */
        uint64_t first_lba;             /* First LBA */
        uint64_t last_lba;              /* Last LBA (inclusive) */
        uint64_t attributes;            /* Attribute flags */
        uint16_t name[36];              /* UTF-16LE partition name */
} gpt_partition_entry_t;

/* Protective MBR structure (LBA 0) */
typedef struct __attribute__((packed)) {
        uint8_t boot_code[440];
        uint32_t disk_signature;
        uint16_t reserved;
        struct {
                uint8_t status;
                uint8_t first_chs[3];
                uint8_t type;
                uint8_t last_chs[3];
                uint32_t first_lba;
                uint32_t num_sectors;
        } partitions[4];
        uint16_t signature;
} protective_mbr_t;

/* GPT partition information */
typedef struct {
        uint8_t type_guid[16];
        uint8_t partition_guid[16];
        uint64_t first_lba;
        uint64_t last_lba;
        uint64_t attributes;
        char name[73];                  /* UTF-8 converted name */
        uint8_t valid;
} gpt_partition_info_t;

/* Function declarations */
int gpt_init(void);
int gpt_create_table(uint64_t total_sectors, const uint8_t disk_guid[16]);
int gpt_add_partition(uint64_t first_lba, uint64_t last_lba,
                      const uint8_t type_guid[16],
                      const char *name);
int gpt_get_partition(uint32_t index, gpt_partition_info_t *info);
int gpt_list_partitions(void (*callback)(uint32_t index,
                                         const gpt_partition_info_t *info));
int gpt_delete_partition(uint32_t index);
int gpt_verify(void);

#endif /* GPT_H */