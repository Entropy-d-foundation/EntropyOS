/*
    GloamOS
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

/*
 * second warning: this file is a compat layer for an ExFAT driver THAT IS NOT NATIVE TO THIS PROJECT, THE OTHER
 * FILES IN THIS FOLDER (and the only subfolder from here) ARE NOT NATIVE. the repo from where i took this code: https://github.com/relan/exfat/tree/master/libexfat
 * I WARNED YOU.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#ifndef __UEFI__
#include <time.h>   /* struct tm, time_t */
#include <sys/types.h>  /* off_t, ssize_t */
#endif
#include "exfatfs.h"  /* le16_t for mkfs */
#include "../../kernel/console.h" /* text() */

#ifdef __UEFI__
#undef off_t
#define off_t __int64
#endif

/* For UEFI builds, include sufficient headers for mkfs stubs */
#ifdef __UEFI__
struct exfat;
struct exfat_node;
struct exfat_entry_meta1;
struct exfat_dev;
#include "mkfs/mkexfat.h"  /* struct fs_object definition */
#else
struct exfat;
struct exfat_node;
struct exfat_entry_meta1;
struct fs_object;
#endif

/* simple bump allocator - reduced heap to prevent stack collision in UEFI */
#define EXFAT_HEAP_SIZE (2 * 1024 * 1024)  /* 2 MB heap for exFAT mkfs - reduced to be safer */
static uint8_t exfat_heap[EXFAT_HEAP_SIZE];
static size_t exfat_heap_used = 0;

void *malloc(size_t size)
{
    size = (size + 7) & ~7;
    if (exfat_heap_used + size > EXFAT_HEAP_SIZE) {
        exfat_error("malloc failed: requested %lu/%lu bytes",
            (unsigned long)size, (unsigned long)(EXFAT_HEAP_SIZE - exfat_heap_used));
        return NULL;
    }
    void *p = exfat_heap + exfat_heap_used;
    exfat_heap_used += size;
    return p;
}

void free(void *ptr)
{
    (void)ptr; /* no-op */
}

/* Debug helper: report current heap usage */
void exfat_heap_report(void)
{
#ifdef __UEFI__
    char msg[80];
    int n = 0;
    const char *prefix = "heap: ";
    while (*prefix && n < 70) msg[n++] = *prefix++;
    
    /* Format heap_used */
    unsigned long h_used = exfat_heap_used;
    unsigned long divisor = 1000000000UL;
    while (divisor > 0) {
        msg[n++] = '0' + (h_used / divisor);
        h_used %= divisor;
        divisor /= 10;
        if (divisor == 0 && h_used > 0) msg[n++] = '.';
        if (divisor && divisor < 1000) break;
    }
    msg[n++] = '/';
    
    /* Format heap_size */
    unsigned long h_total = EXFAT_HEAP_SIZE;
    divisor = 1000000000UL;
    while (divisor > 0) {
        msg[n++] = '0' + (h_total / divisor);
        h_total %= divisor;
        divisor /= 10;
        if (divisor == 0 && h_total > 0) msg[n++] = '.';
        if (divisor && divisor < 1000) break;
    }
    msg[n++] = ' ';
    msg[n++] = 'b';
    msg[n++] = 'y';
    msg[n++] = 't';
    msg[n++] = 'e';
    msg[n++] = 's';
    msg[n] = 0;
    exfat_error(msg);
#endif
}

/* Reset heap allocator - call after mkfs completes */
void exfat_heap_reset(void)
{
    exfat_heap_used = 0;
}

/* string and memory helpers */

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s && *s++)
        n++;
    return n;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n && *src)
    {
        *d++ = *src++;
        n--;
    }
    while (n--)
        *d++ = '\0';
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    while (n--)
    {
        if (*p != *q)
            return *p - *q;
        p++; q++;
    }
    return 0;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s2 && *s1 == *s2)
    {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

#ifndef __UEFI__
#else
#endif

/* Common string functions for both UEFI and non-UEFI builds */
char *strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return (char*)haystack;
    for (; *haystack; haystack++)
    {
        if (haystack[0] == needle[0] &&
            strncmp(haystack, needle, nlen) == 0)
            return (char*)haystack;
    }
    return NULL;
}

long strtol(const char *nptr, char **endptr, int base)
{
    /* simplistic implementation supporting only base 10 */
    long result = 0;
    int sign = 1;
    const char *p = nptr;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9')
    {
        result = result * base + (*p - '0');
        p++;
    }
    if (endptr) *endptr = (char*)p;
    return sign * result;
}

size_t strspn(const char *s, const char *accept)
{
    size_t count = 0;
    while (*s && strchr(accept, *s))
    {
        count++;
        s++;
    }
    return count;
}

char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c)
            return (char*)s;
        s++;
    }
    return NULL;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

int stricmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = *a < 'A' || *a > 'Z' ? *a : *a - 'A' + 'a';
        char cb = *b < 'A' || *b > 'Z' ? *b : *b - 'A' + 'a';
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* time-related stubs */
void tzset(void) { /* no timezone support */ }

/* fgets stub (both UEFI and non-UEFI) */
char *fgets(char *s, int n, void *stream)
{
    /* no input available in UEFI environment */
    (void)stream; (void)n;
    if (s) *s = '\0';
    return NULL;
}

/* I/O stubs that route output to the kernel text logger */
int puts(const char *s)
{
    if (s) text(s);
    text("\n");
    return 0;
}

int fputs(const char *s, FILE *stream)
{
    (void)stream;
    if (s) text(s);
    return 0;
}

int fflush(FILE *stream)
{
    (void)stream;
    return 0;
}

/* FILE stream stubs */
FILE __stdin = {0};
FILE __stdout = {0};
FILE __stderr = {0};

/* getopt stubs */
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int printf(const char *fmt, ...)
{
    (void)fmt;
    /* drop everything; formatting in UEFI would require vsnprintf anyway */
    return 0;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    (void)stream; (void)fmt;
    /* drop everything */
    return 0;
}

void exit(int status)
{
    (void)status;
    /* no-op in UEFI */
}
#ifdef __UEFI__
/* Define struct timeval when needed */
struct timeval {
    time_t tv_sec;
    long tv_usec;
};
#endif

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; }
    return 0;
}

time_t time(time_t *tloc)
{
    time_t now = 0;  /* Return 0 as current time in UEFI */
    if (tloc) *tloc = now;
    return now;
}

/* Make sure time() is referenced and available */
time_t (*_time_ptr)(time_t *tloc) = time;

/* POSIX I/O stubs for UEFI (normally provided by libc) */
#ifndef off64_t
#define off64_t off_t
#endif
off64_t lseek64(int fd, off64_t offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    return 0;  /* stub: no seeking in UEFI environment */
}

/* Stubs for repair functions (already defined in repair.c, but UEFI needs versions if repair.c isn't linked) */
/* NOTE: These are commented out because repair.c provides the real implementations when linked */
/*
bool exfat_ask_to_fix(const struct exfat *ef)
{
    (void)ef;
    return false;
}

bool exfat_fix_invalid_vbr_checksum(const struct exfat *ef, void* sector,
                                     uint32_t vbr_checksum)
{
    (void)ef; (void)sector; (void)vbr_checksum;
    return false;
}

bool exfat_fix_invalid_node_checksum(const struct exfat *ef,
                                      struct exfat_node *node)
{
    (void)ef; (void)node;
    return false;
}

bool exfat_fix_unknown_entry(struct exfat *ef,
                              struct exfat_node* dir,
                              const struct exfat_entry *entry,
                              off_t offset)
{
    (void)ef; (void)dir; (void)entry; (void)offset;
    return false;
}
*/

/* Define stricmp if not already defined */
#ifndef stricmp
#define stricmp(a, b) stricmp_compat(a, b)
static int stricmp_compat(const char *a, const char *b)
{
    int ca, cb;
    do {
        ca = *a >= 'A' && *a <= 'Z' ? *a - 'A' + 'a' : *a;
        cb = *b >= 'A' && *b <= 'Z' ? *b - 'A' + 'a' : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    } while (ca);
    return 0;
}
#endif

/* Define strcasecmp - same as stricmp for case-insensitive string comparison */
#ifdef __UEFI__
int strcasecmp(const char *a, const char *b)
{
    return stricmp(a, b);
}
#endif

/* mkfs objects array - actual implementations compiled from vbr.c, fat.c, cbm.c, uct.c, rootdir.c */
extern const struct fs_object vbr;
extern const struct fs_object fat;
extern const struct fs_object cbm;
extern const struct fs_object uct;
extern const struct fs_object rootdir;

const struct fs_object* objects[] = { &vbr, &vbr, &fat, &cbm, &uct, &rootdir, NULL };

/* Mkfs getter functions from main.c (when compiled) or basic stubs for UEFI.*/
#ifdef __UEFI__
/* Parameter structure for mkfs in UEFI mode */
static struct {
    int sector_bits;
    int spc_bits;
    off_t volume_size;
    le16_t volume_label[EXFAT_ENAME_MAX + 1];
    uint32_t volume_serial;
    uint64_t first_sector;
} mkfs_param = {
    .sector_bits = 9,      /* 512-byte sectors */
    .spc_bits = 3,         /* 8 sectors per cluster = 4KB clusters */
    .volume_size = 0,      /* set by exfat_format */
    .volume_serial = 0x12345678,
    .first_sector = 0,
};

void exfat_mkfs_set_volume_size(off_t size)
{
    mkfs_param.volume_size = size;
    /* Validate to prevent overflow */
    if (size == 0)
        exfat_error("invalid volume size: 0");
}

int get_sector_bits(void) { return mkfs_param.sector_bits; }
int get_spc_bits(void) { return mkfs_param.spc_bits; }
off_t get_volume_size(void) { return mkfs_param.volume_size; }
const le16_t* get_volume_label(void) { return mkfs_param.volume_label; }
uint32_t get_volume_serial(void) { return mkfs_param.volume_serial; }
uint64_t get_first_sector(void) { return mkfs_param.first_sector; }
int get_sector_size(void) { return 512; }
int get_cluster_size(void) { return 4096; }
#endif

/* get_position referenced by mkfs utilities */
/* NOTE: mkexfat.c provides the real implementation */

int exfat_fsck(void)
{
    /* Stub: filesystem check always passes in our minimal implementation */
    return 0;
}
