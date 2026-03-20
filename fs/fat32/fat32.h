/* SPDX-License-Identifier: GPL-3.0 */
#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

/* FAT32 Boot Sector Structure */
typedef struct {
    uint8_t  jmp[3];              /* Jump instruction */
    uint8_t  oem[8];              /* OEM name */
    uint16_t bytes_per_sector;    /* Bytes per sector */
    uint8_t  sectors_per_cluster; /* Sectors per cluster */
    uint16_t reserved_sectors;    /* Reserved sectors */
    uint8_t  num_fats;            /* Number of FATs */
    uint16_t root_entries;        /* Root directory entries (0 for FAT32) */
    uint16_t total_sectors_16;    /* Total sectors (0 for FAT32) */
    uint8_t  media_type;          /* Media descriptor */
    uint16_t fat_size_16;         /* FAT size in sectors (0 for FAT32) */
    uint16_t sectors_per_track;   /* Sectors per track */
    uint16_t num_heads;           /* Number of heads */
    uint32_t hidden_sectors;      /* Hidden sectors */
    uint32_t total_sectors_32;    /* Total sectors */
    /* FAT32 specific */
    uint32_t fat_size_32;         /* FAT size in sectors */
    uint16_t ext_flags;           /* Extended flags */
    uint16_t fs_version;          /* Filesystem version */
    uint32_t root_cluster;        /* Root directory cluster */
    uint16_t fs_info;             /* FSInfo sector */
    uint16_t backup_boot;         /* Backup boot sector */
    uint8_t  reserved[12];        /* Reserved */
    uint8_t  drive_number;        /* Drive number */
    uint8_t  reserved1;           /* Reserved */
    uint8_t  boot_signature;      /* Boot signature (0x29) */
    uint32_t volume_id;           /* Volume ID */
    uint8_t  volume_label[11];    /* Volume label */
    uint8_t  fs_type[8];          /* Filesystem type */
} __attribute__((packed)) fat32_boot_sector_t;

/* FAT32 Directory Entry */
typedef struct {
    uint8_t  name[11];            /* Filename (8.3 format) */
    uint8_t  attr;                /* Attributes */
    uint8_t  nt_reserved;         /* Reserved for NT */
    uint8_t  create_time_tenth;   /* Creation time (tenths of second) */
    uint16_t create_time;         /* Creation time */
    uint16_t create_date;         /* Creation date */
    uint16_t last_access_date;    /* Last access date */
    uint16_t first_cluster_hi;    /* High word of first cluster */
    uint16_t write_time;          /* Last write time */
    uint16_t write_date;          /* Last write date */
    uint16_t first_cluster_lo;    /* Low word of first cluster */
    uint32_t file_size;           /* File size in bytes */
} __attribute__((packed)) fat32_dir_entry_t;

/* Directory Entry Attributes */
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

/* Special cluster values */
#define FAT32_EOC       0x0FFFFFF8  /* End of chain */
#define FAT32_BAD       0x0FFFFFF7  /* Bad cluster */
#define FAT32_FREE      0x00000000  /* Free cluster */

/* FAT32 filesystem state */
typedef struct {
    uint32_t fat_begin_lba;       /* First FAT sector */
    uint32_t cluster_begin_lba;   /* First cluster sector */
    uint32_t sectors_per_cluster; /* Sectors per cluster */
    uint32_t root_dir_cluster;    /* Root directory cluster */
    uint32_t fat_size;            /* FAT size in sectors */
    uint32_t num_fats;            /* Number of FATs */
    uint32_t total_clusters;      /* Total clusters */
    uint8_t  initialized;         /* Initialization flag */
} fat32_fs_t;

/* Public API */
void fat32_set_partition_offset(uint64_t offset);
int  fat32_format(uint64_t total_sectors, const char *volume_label);
int  fat32_init(void);
int  fat32_read_file(const char *filename, void *buffer, uint32_t max_size, uint32_t *bytes_read);
/* Read file from a specific directory cluster. Reads up to max_size bytes and sets bytes_read to actual bytes read. */
int  fat32_read_file_in(uint32_t parent_cluster, const char *filename, void *buffer, uint32_t max_size, uint32_t *bytes_read);
int  fat32_write_file(const char *filename, const void *buffer, uint32_t size);
/* Write a file into a specific directory cluster */
int  fat32_write_file_in(uint32_t parent_cluster, const char *filename, const void *buffer, uint32_t size);
/* Get the size of a file in a specific directory cluster (for verification) */
int  fat32_get_file_size_in(uint32_t parent_cluster, const char *filename, uint32_t *size);
/* Compare a file in a specific directory cluster with expected bytes. If mismatch,
 * returns -1 and sets mismatch_offset. If snip buffers are provided and snip_len>0,
 * fills them with up to snip_len bytes from the file and expected data at mismatch.
 */
int  fat32_compare_file_in(uint32_t parent_cluster, const char *filename, const void *expected, uint32_t expected_size, uint32_t *mismatch_offset, uint8_t *disk_snip, uint8_t *expected_snip, uint32_t snip_len);
int  fat32_create_directory(const char *dirname);
int  fat32_create_directory_in(uint32_t parent_cluster, const char *dirname);
int  fat32_delete_directory(const char *dirname);
int  fat32_delete_directory_in(uint32_t parent_cluster, const char *dirname);
int  fat32_delete_file(const char *filename);
int  fat32_list_files(void (*callback)(const char *name, uint32_t size, uint8_t is_dir));

/* Helper functions */
uint32_t fat32_cluster_to_lba(uint32_t cluster);
int      fat32_read_cluster(uint32_t cluster, void *buffer);
int      fat32_write_cluster(uint32_t cluster, const void *buffer);
uint32_t fat32_get_next_cluster(uint32_t cluster);
int      fat32_set_next_cluster(uint32_t cluster, uint32_t next);
uint32_t fat32_allocate_cluster(void);
int      fat32_free_cluster(uint32_t cluster);
uint32_t fat32_get_root_cluster(void);
uint32_t fat32_find_directory(uint32_t parent_cluster, const char *dirname);

#endif /* FAT32_H */