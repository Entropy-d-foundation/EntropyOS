/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GloamOS/fs/exfat/exfat.c
 *
 * Thin wrapper around the relan/exfat library to expose the subset of
 * functionality used by GloamOS (formatting, simple file/directory
 * manipulation).  The original library operates on file descriptors and
 * assumes a POSIX environment; the kernel/UEFI components of this project
 * instead read and write sectors through the block driver.  We therefore
 * add a small glue layer that translates the library calls into sector I/O
 * and provide higher-level helpers that are convenient for the bootloader
 * and installer.
 *
 * Copyright (C) 2026 Gabriel S\u00eerbu
 */

#include "exfat.h"
#include "../../drivers/block/block.h"
#include "../../drivers/sata/sata.h"
#include "../../kernel/console.h"   /* for text(), debug output */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mkfs/mkexfat.h"  /* declares mkfs */

/* Global state for the mounted filesystem.  The library requires an
   "struct exfat" for every operation. */
static struct exfat g_ef;
static int g_initialized = 0;

/* When the device is opened through exfat_open(NULL, ...), the returned
   struct exfat_dev already has a suitable size field; we don't keep a
   separate pointer for it after mount. */

/* Helper to perform lookup under an arbitrary directory cluster.  The
   library's exfat_lookup always begins at ef->root, so we temporarily
   swap in a synthetic root node representing the supplied cluster.  The
   returned node, if any, must be freed by the caller with exfat_put_node(). */
static int lookup_in_cluster(uint32_t cluster,
                             struct exfat_node **out_node,
                             const char *path)
{
    struct exfat_node *saved_root = g_ef.root;
    struct exfat_node *temp = malloc(sizeof(*temp));
    if (!temp)
        return -ENOMEM;
    memset(temp, 0, sizeof(*temp));
    temp->attrib = EXFAT_ATTRIB_DIR;
    temp->start_cluster = cluster;
    temp->fptr_cluster = cluster;
    temp->name[0] = cpu_to_le16('\0');
    temp->references = 0;
    temp->valid_size = 0; /* not needed for lookup */

    g_ef.root = temp;
    int rc = exfat_lookup(&g_ef, out_node, path);
    g_ef.root = saved_root;
    free(temp);
    return rc;
}

int exfat_format(uint64_t total_sectors, const char *volume_label)
{
    if (!block_is_present())
    {
        text("ExFAT: no block device\n");
        return -1;
    }

    text("ExFAT: formatting partition...\n");

    struct exfat_dev *dev = exfat_open(NULL, EXFAT_MODE_RW);
    if (!dev)
    {
        text("ExFAT: unable to open device for mkfs\n");
        return -1;
    }

    /* volume size in bytes */
    off_t volsize = (off_t) total_sectors * 512ULL;
    
    /* Initialize mkfs parameters for UEFI */
#ifdef __UEFI__
    extern void exfat_mkfs_set_volume_size(off_t size);
    exfat_mkfs_set_volume_size(volsize);
#endif
    
    int rc = mkfs(dev, volsize);
    
    if (rc != 0)
    {
        text("ExFAT: mkfs failed\n");
        exfat_close(dev);
        return -1;
    }
    
    text("ExFAT: format complete\n");
    exfat_close(dev);
    
    /* Flush write cache and add delay to ensure persistence */
    sata_flush_cache();
    /* Allow multiple delay iterations for cache to flush */
    for (volatile int delay_loop = 0; delay_loop < 500000; ++delay_loop);
    
    /* Reset heap allocator after formatting */
#ifdef __UEFI__
    exfat_heap_reset();
#endif
    
    return 0;
}

int exfat_init(void)
{
    if (!block_is_present())
        return -1;

    memset(&g_ef, 0, sizeof(g_ef));
    if (exfat_mount(&g_ef, NULL, "") != 0)
        return -1;

    g_initialized = 1;
    return 0;
}

int exfat_create_directory(const char *dirname)
{
    if (!g_initialized)
        return -1;
    return exfat_mkdir(&g_ef, dirname);
}

int exfat_create_dir_path(const char *path)
{
    if (!g_initialized)
        return -1;
    /* iterate components and mkdir each prefix */
    char comp[EXFAT_NAME_MAX + 1];
    const char *p = path;
    uint32_t plen = strlen(path);
    uint32_t idx = 0;

    while (idx < plen)
    {
        /* copy up to next slash */
        uint32_t i = 0;
        while (idx < plen && path[idx] != '/')
        {
            if (i < EXFAT_NAME_MAX) comp[i++] = path[idx];
            idx++;
        }
        comp[i] = '\0';
        if (i > 0)
        {
            /* build prefix buffer */
            char prefix[EXFAT_NAME_MAX * 32];
            /* reconstruct prefix from scratch for each component */
            strncpy(prefix, path, idx);
            prefix[idx] = '\0';
            /* attempt to create; ignore EEXIST */
            int rc = exfat_mkdir(&g_ef, prefix);
            if (rc != 0 && rc != -EEXIST)
                return -1;
        }
        idx++; /* skip slash */
    }
    
    /* After creating all directories, remount to reload the filesystem
       from disk and clear all in-memory caches */
    exfat_unmount(&g_ef);
    memset(&g_ef, 0, sizeof(g_ef));
    if (exfat_mount(&g_ef, NULL, "") != 0)
    {
        g_initialized = 0;
        return -1;
    }
    
    return 0;
}

int exfat_get_file_info(const char *path, exfat_file_info_t *info)
{
    if (!g_initialized || !info)
        return -1;
    struct exfat_node *node = NULL;
    if (exfat_lookup(&g_ef, &node, path) != 0)
    {
        info->valid = 0;
        return -1;
    }
    info->valid = 1;
    info->size = node->size;
    info->is_directory = (node->attrib & EXFAT_ATTRIB_DIR) ? 1 : 0;
    info->first_cluster = node->start_cluster;
    exfat_put_node(&g_ef, node);
    return 0;
}

int exfat_get_file_info_in(uint32_t parent_cluster, const char *name,
                           exfat_file_info_t *info)
{
    if (!g_initialized || !info)
        return -1;
    struct exfat_node *node = NULL;
    if (lookup_in_cluster(parent_cluster, &node, name) != 0)
    {
        info->valid = 0;
        return -1;
    }
    info->valid = 1;
    info->size = node->size;
    info->is_directory = (node->attrib & EXFAT_ATTRIB_DIR) ? 1 : 0;
    info->first_cluster = node->start_cluster;
    exfat_put_node(&g_ef, node);
    return 0;
}

int exfat_write_file(const char *name, const void *data, uint32_t size)
{
    if (!g_initialized)
        return -1;
    struct exfat_node *node = NULL;
    int rc = exfat_lookup(&g_ef, &node, name);
    if (rc != 0)
    {
        /* create new file */
        if (exfat_mknod(&g_ef, name) != 0)
            return -1;
        if (exfat_lookup(&g_ef, &node, name) != 0)
            return -1;
    }
    if (node->attrib & EXFAT_ATTRIB_DIR)
    {
        exfat_put_node(&g_ef, node);
        return -1;
    }
    if (exfat_truncate(&g_ef, node, size, true) != 0)
    {
        exfat_put_node(&g_ef, node);
        return -1;
    }
    ssize_t written = exfat_generic_pwrite(&g_ef, node, data, size, 0);
    exfat_flush_node(&g_ef, node);
    exfat_put_node(&g_ef, node);
    return (written == (ssize_t)size) ? 0 : -1;
}

int exfat_write_file_in(uint32_t parent_cluster, const char *name,
                        const void *data, uint32_t size)
{
    if (!g_initialized)
        return -1;
    struct exfat_node *node = NULL;
    int rc = lookup_in_cluster(parent_cluster, &node, name);
    if (rc != 0)
    {
        /* create file under parent cluster */
        /* temporarily set root to parent_cluster and call mknod */
        struct exfat_node *saved_root = g_ef.root;
        struct exfat_node *temp = malloc(sizeof(*temp));
        if (!temp)
            return -1;
        memset(temp, 0, sizeof(*temp));
        temp->attrib = EXFAT_ATTRIB_DIR;
        temp->start_cluster = parent_cluster;
        temp->fptr_cluster = parent_cluster;
        temp->name[0] = cpu_to_le16('\0');
        temp->references = 0;
        g_ef.root = temp;
        rc = exfat_mknod(&g_ef, name);
        g_ef.root = saved_root;
        free(temp);
        if (rc != 0)
            return -1;
        /* look up again to get the node pointer */
        rc = lookup_in_cluster(parent_cluster, &node, name);
        if (rc != 0)
            return -1;
    }
    if (node->attrib & EXFAT_ATTRIB_DIR)
    {
        exfat_put_node(&g_ef, node);
        return -1;
    }
    if (exfat_truncate(&g_ef, node, size, true) != 0)
    {
        exfat_put_node(&g_ef, node);
        return -1;
    }
    ssize_t written = exfat_generic_pwrite(&g_ef, node, data, size, 0);
    exfat_flush_node(&g_ef, node);
    exfat_put_node(&g_ef, node);
    return (written == (ssize_t)size) ? 0 : -1;
}

int exfat_read_file(const char *filename, void *buffer, uint32_t max_size, uint32_t *bytes_read)
{
    if (bytes_read)
        *bytes_read = 0;

    if (!g_initialized || !filename || !buffer || max_size == 0 || !bytes_read)
        return -1;

    struct exfat_node *node = NULL;
    if (exfat_lookup(&g_ef, &node, filename) != 0)
        return -1;

    if (node->attrib & EXFAT_ATTRIB_DIR)
    {
        exfat_put_node(&g_ef, node);
        return -1;
    }

    uint64_t size64 = node->size;
    uint32_t to_read = (size64 < (uint64_t) max_size) ? (uint32_t) size64 : max_size;

    ssize_t r = exfat_generic_pread(&g_ef, node, buffer, to_read, 0);
    exfat_put_node(&g_ef, node);

    if (r <= 0)
        return -1;

    *bytes_read = (uint32_t) r;
    return 0;
}

int exfat_list_files(void (*callback)(const char *name, uint64_t size, uint8_t is_dir))
{
    if (!g_initialized || !callback || g_ef.root == NULL)
        return -1;

    struct exfat_iterator it;
    if (exfat_opendir(&g_ef, g_ef.root, &it) != 0)
        return -1;

    struct exfat_node *node;
    char name[EXFAT_UTF8_NAME_BUFFER_MAX];

    while ((node = exfat_readdir(&it)) != NULL)
    {
        exfat_get_name(node, name);
        uint8_t is_dir = (node->attrib & EXFAT_ATTRIB_DIR) ? 1 : 0;
        uint64_t size = node->size;
        callback(name, size, is_dir);
        exfat_put_node(&g_ef, node);
    }

    exfat_closedir(&g_ef, &it);
    return 0;
}
