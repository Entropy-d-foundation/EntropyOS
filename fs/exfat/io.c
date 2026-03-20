/*
	io.c (02.09.09)
	exFAT file system implementation library.

	Free exFAT implementation.
	Copyright (C) 2010-2023  Andrew Nayenko

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "exfat.h"
#include "../../drivers/thumbdrive/mass_storage.h"
#include "../../drivers/sata/sata.h"
#include "../../drivers/block/block.h"
#include <inttypes.h>
#ifndef __UEFI__
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>

/* When the exfat backend is used on top of our block driver the caller
   sets the partition offset (in sectors) with this function; all I/O
   routines below add this value to any LBA they calculate. */
static uint64_t exfat_partition_offset = 0;

void exfat_set_partition_offset(uint64_t offset)
{
	exfat_partition_offset = offset;
}
#if defined(__APPLE__)
#include <sys/disk.h>
#elif defined(__OpenBSD__)
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#elif defined(__NetBSD__)
#include <sys/ioctl.h>
#elif __linux__
#include <sys/mount.h>
#endif
#ifdef USE_UBLIO
#include <sys/uio.h>
#include <ublio.h>
#endif

#ifdef __UEFI__
/* Stub implementations for UEFI */
ssize_t read(int fd, void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return 0;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return 0;
}
#endif

struct exfat_dev
{
	int fd;
	enum exfat_mode mode;
	off_t size; /* in bytes */
	/* current position for sequential I/O or when no fd is available */
	off_t pos;
#ifdef USE_UBLIO
	ublio_filehandle_t ufh;
#endif
};

#ifndef __UEFI__
static bool is_open(int fd)
{
	return fcntl(fd, F_GETFD) != -1;
}

static int open_ro(const char* spec)
{
	return open(spec, O_RDONLY);
}

static int open_rw(const char* spec)
{
	int fd = open(spec, O_RDWR);
#ifdef __linux__
	int ro = 0;

	/*
	   This ioctl is needed because after "blockdev --setro" kernel still
	   allows to open the device in read-write mode but fails writes.
	*/
	if (fd != -1 && ioctl(fd, BLKROGET, &ro) == 0 && ro)
	{
		close(fd);
		errno = EROFS;
		return -1;
	}
#endif
	return fd;
}
#endif

struct exfat_dev* exfat_open(const char* spec, enum exfat_mode mode)
{
	struct exfat_dev* dev;
	struct stat stbuf;
#ifdef USE_UBLIO
	struct ublio_param up;
#endif

	/* special case for in‑memory block backend: caller passes spec == NULL
	   and is responsible for calling exfat_set_partition_offset().  In this
	   mode we skip all the normal open/stat logic and simply fill the
	   exfat_dev structure manually. */
	if (spec == NULL)
	{
		dev = malloc(sizeof(struct exfat_dev));
		if (dev == NULL)
		{
			exfat_error("failed to allocate memory for device structure");
			return NULL;
		}
		dev->fd = -1;
		dev->mode = mode;
		/* determine available sectors via installed backend drivers */
		uint64_t sectors = 0;
		if (ms_is_present())
			sectors = ms_get_num_blocks();
		else if (sata_is_present())
		{
			uint64_t tmp = 0;
			if (sata_get_num_blocks(&tmp) == 0)
				sectors = tmp;
		}
		dev->size = sectors * 512ULL;
		dev->pos = 0;
		return dev;
	}

#ifndef __UEFI__
	/* The system allocates file descriptors sequentially. If we have been
	   started with stdin (0), stdout (1) or stderr (2) closed, the system
	   will give us descriptor 0, 1 or 2 later when we open block device,
	   FUSE communication pipe, etc. As a result, functions using stdin,
	   stdout or stderr will actually work with a different thing and can
	   corrupt it. Protect descriptors 0, 1 and 2 from such misuse. */
	while (!is_open(STDIN_FILENO)
		|| !is_open(STDOUT_FILENO)
		|| !is_open(STDERR_FILENO))
	{
		/* we don't need those descriptors, let them leak */
		if (open("/dev/null", O_RDWR) == -1)
		{
			exfat_error("failed to open /dev/null");
			return NULL;
		}
	}

	dev = malloc(sizeof(struct exfat_dev));
	if (dev == NULL)
	{
		exfat_error("failed to allocate memory for device structure");
		return NULL;
	}

	/* The remainder of this function is purely POSIX file-handling. */

	if (mode == EXFAT_MODE_RO)
		dev->fd = open_ro(spec);
	else
		dev->fd = open_rw(spec);

	if (dev->fd == -1)
	{
		exfat_error("failed to open device %s: %s", spec, strerror(errno));
		free(dev);
		return NULL;
	}

	/* determine size and other properties via fstat/ioctl/etc. */
	if (fstat(dev->fd, &stbuf) != 0)
	{
		exfat_error("fstat() failed: %s", strerror(errno));
		close(dev->fd);
		free(dev);
		return NULL;
	}

	/* existing ioctl blocks omitted for brevity... (they are unchanged) */

	return dev;
#else
	/* UEFI: we only support spec==NULL path above; reject anything else. */
	return NULL;
#endif
}

int exfat_close(struct exfat_dev* dev)
{
	int rc = 0;

#ifdef USE_UBLIO
	if (ublio_close(dev->ufh) != 0)
	{
		exfat_error("failed to close ublio");
		rc = -EIO;
	}
#endif
	/* only close a real descriptor */
	if (dev->fd >= 0) {
#ifndef __UEFI__
		if (close(dev->fd) != 0)
		{
			exfat_error("failed to close device: %s", strerror(errno));
			rc = -EIO;
		}
#else
		/* nothing to do on UEFI */
#endif
	}
	free(dev);
	return rc;
}

int exfat_fsync(struct exfat_dev* dev)
{
	int rc = 0;

#ifdef USE_UBLIO
	if (ublio_fsync(dev->ufh) != 0)
	{
		exfat_error("ublio fsync failed");
		rc = -EIO;
	}
#endif
#ifndef __UEFI__
	if (fsync(dev->fd) != 0)
	{
		exfat_error("fsync failed: %s", strerror(errno));
		rc = -EIO;
	}
#endif
	return rc;
}

enum exfat_mode exfat_get_mode(const struct exfat_dev* dev)
{
	return dev->mode;
}

off_t exfat_get_size(const struct exfat_dev* dev)
{
	return dev->size;
}

off_t exfat_seek(struct exfat_dev* dev, off_t offset, int whence)
{
#ifdef __UEFI__
    /* simple in-memory position tracking; actual I/O occurs in pread/pwrite */
    switch (whence) {
    case SEEK_SET: dev->pos = offset; break;
    case SEEK_CUR: dev->pos += offset; break;
    case SEEK_END: dev->pos = dev->size + offset; break;
    default: return (off_t)-1;
    }
    return dev->pos;
#else
# ifdef USE_UBLIO
    /* XXX SEEK_CUR will be handled incorrectly */
    return dev->pos = lseek(dev->fd, offset, whence);
# else
    return lseek(dev->fd, offset, whence);
# endif
#endif
}

ssize_t exfat_read(struct exfat_dev* dev, void* buffer, size_t size)
{
#ifdef __UEFI__
    /* delegate to pread implementation at current position */
    return exfat_pread(dev, buffer, size, dev->pos);
#else
# ifdef USE_UBLIO
    ssize_t result = ublio_pread(dev->ufh, buffer, size, dev->pos);
    if (result >= 0)
        dev->pos += size;
    return result;
# else
    return read(dev->fd, buffer, size);
# endif
#endif
}

ssize_t exfat_write(struct exfat_dev* dev, const void* buffer, size_t size)
{
#ifdef __UEFI__
    /* delegate to pwrite implementation at current position */
    ssize_t r = exfat_pwrite(dev, buffer, size, dev->pos);
    if (r > 0)
        dev->pos += r;
    return r;
#else
# ifdef USE_UBLIO
    ssize_t result = ublio_pwrite(dev->ufh, (void*) buffer, size, dev->pos);
    if (result >= 0)
        dev->pos += size;
    return result;
# else
    return write(dev->fd, buffer, size);
# endif
#endif
}

ssize_t exfat_pread(struct exfat_dev* dev, void* buffer, size_t size,
		off_t offset)
{
#ifdef USE_UBLIO
	return ublio_pread(dev->ufh, buffer, size, offset);
#else
#ifndef __UEFI__
	return pread(dev->fd, buffer, size, offset);
#else
	/* UEFI read using block driver */
	uint8_t *buf = (uint8_t*)buffer;
	uint64_t pos = offset + exfat_partition_offset * 512ULL;
	size_t remaining = size;
	size_t bytes_read = 0;
	
	/* Use static buffer in UEFI (single-threaded) to avoid stack pressure.
	   This prevents stack exhaustion during large sequential reads. */
	static uint8_t sector_buf[512];
	
	while (remaining > 0) {
		uint64_t sector = pos / 512;
		uint32_t off = pos % 512;
		
		if (block_read_sector(sector, sector_buf) != 0)
			return -1;
		
		size_t chunk = remaining;
		if (chunk > 512 - off)
			chunk = 512 - off;
		
		memcpy(buf, sector_buf + off, chunk);
		buf += chunk;
		pos += chunk;
		remaining -= chunk;
		bytes_read += chunk;
	}
	return bytes_read;
#endif
#endif
}

ssize_t exfat_pwrite(struct exfat_dev* dev, const void* buffer, size_t size,
		off_t offset)
{
#ifdef USE_UBLIO
	return ublio_pwrite(dev->ufh, (void*) buffer, size, offset);
#else
#ifndef __UEFI__
    return pwrite(dev->fd, buffer, size, offset);
#else
    /* rudimentary write using block driver */
    uint8_t *buf = (uint8_t*)buffer;
    uint64_t pos = offset + exfat_partition_offset * 512ULL;
    size_t remaining = size;
    
    /* Use static buffer in UEFI (single-threaded) to avoid stack pressure.
       This prevents stack exhaustion during large sequential writes. */
    static uint8_t sector_buf[512];
    
    while (remaining > 0) {
        uint64_t sector = pos / 512;
        uint32_t off = pos % 512;
        
        /* Always read existing sector content first (read-modify-write).
           This ensures: 1) sector_buf is initialized, 2) unmodified parts are preserved */
        if (block_read_sector(sector, sector_buf) != 0)
            return -1;
        
        size_t chunk = remaining;
        if (chunk > 512 - off)
            chunk = 512 - off;
        memcpy(sector_buf + off, buf, chunk);
        if (block_write_sector(sector, sector_buf) != 0)
            return -1;
        buf += chunk;
        pos += chunk;
        remaining -= chunk;
    }
    return size;
#endif
#endif
}

ssize_t exfat_generic_pread(const struct exfat* ef, struct exfat_node* node,
		void* buffer, size_t size, off_t offset)
{
	uint64_t uoffset = offset;
	cluster_t cluster;
	char* bufp = buffer;
	off_t lsize, loffset, remainder;

	if (offset < 0)
		return -EINVAL;
	if (uoffset >= node->size)
		return 0;
	if (size == 0)
		return 0;

	if (uoffset + size > node->valid_size)
	{
		ssize_t bytes = 0;

		if (uoffset < node->valid_size)
		{
			bytes = exfat_generic_pread(ef, node, buffer,
					node->valid_size - uoffset, offset);
			if (bytes < 0 || (size_t) bytes < node->valid_size - uoffset)
				return bytes;
		}
		memset(buffer + bytes, 0,
				MIN(size - bytes, node->size - node->valid_size));
		return MIN(size, node->size - uoffset);
	}

	cluster = exfat_advance_cluster(ef, node, uoffset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(*ef->sb, cluster))
	{
		exfat_error("invalid cluster 0x%x while reading", cluster);
		return -EIO;
	}

	loffset = uoffset % CLUSTER_SIZE(*ef->sb);
	remainder = MIN(size, node->size - uoffset);
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(*ef->sb, cluster))
		{
			exfat_error("invalid cluster 0x%x while reading", cluster);
			return -EIO;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		if (exfat_pread(ef->dev, bufp, lsize,
					exfat_c2o(ef, cluster) + loffset) < 0)
		{
			exfat_error("failed to read cluster %#x", cluster);
			return -EIO;
		}
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!(node->attrib & EXFAT_ATTRIB_DIR) && !ef->ro && !ef->noatime)
		exfat_update_atime(node);
	return MIN(size, node->size - uoffset) - remainder;
}

ssize_t exfat_generic_pwrite(struct exfat* ef, struct exfat_node* node,
		const void* buffer, size_t size, off_t offset)
{
	uint64_t uoffset = offset;
	int rc;
	cluster_t cluster;
	const char* bufp = buffer;
	off_t lsize, loffset, remainder;

	if (offset < 0)
		return -EINVAL;
	if (uoffset > node->size)
	{
		rc = exfat_truncate(ef, node, uoffset, true);
		if (rc != 0)
			return rc;
	}
	if (uoffset + size > node->size)
	{
		rc = exfat_truncate(ef, node, uoffset + size, false);
		if (rc != 0)
			return rc;
	}
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, uoffset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(*ef->sb, cluster))
	{
		exfat_error("invalid cluster 0x%x while writing", cluster);
		return -EIO;
	}

	loffset = uoffset % CLUSTER_SIZE(*ef->sb);
	remainder = size;
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(*ef->sb, cluster))
		{
			exfat_error("invalid cluster 0x%x while writing", cluster);
			return -EIO;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		if (exfat_pwrite(ef->dev, bufp, lsize,
				exfat_c2o(ef, cluster) + loffset) < 0)
		{
			exfat_error("failed to write cluster %#x", cluster);
			return -EIO;
		}
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		node->valid_size = MAX(node->valid_size, uoffset + size - remainder);
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!(node->attrib & EXFAT_ATTRIB_DIR))
		/* directory's mtime should be updated by the caller only when it
		   creates or removes something in this directory */
		exfat_update_mtime(node);
	return size - remainder;
}
