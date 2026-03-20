/* SPDX-License-Identifier: GPL-2.0 */
#include "../../drivers/sata/sata.h"
#include "../../kernel/console.h"
#include "debug_serial.h"
#include "../pcie/pcie.h"
#include "../pci/pci.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ── Global state ─────────────────────────────────────────────────────────── */
struct pcie_device sata_controller;
int sata_controller_found = 0;

/* ── AHCI generic register offsets ───────────────────────────────────────── */
#define AHCI_GHC_OFFSET         0x04        /* Global Host Control             */
#define AHCI_GHC_AE             (1u << 31)  /* AHCI Enable                     */
#define AHCI_PI_OFFSET          0x0C        /* Ports Implemented               */
#define AHCI_PORT_BASE          0x100       /* Port 0 MMIO base offset         */
#define AHCI_PORT_SIZE          0x80        /* Each port occupies 0x80 bytes   */

/* ── Port register offsets (relative to port MMIO base) ──────────────────── */
#define PORT_CLB    0x00    /* Command List Base Address (low)  */
#define PORT_CLBU   0x04    /* Command List Base Address (high) */
#define PORT_FB     0x08    /* FIS Base Address (low)           */
#define PORT_FBU    0x0C    /* FIS Base Address (high)          */
#define PORT_IS     0x10    /* Interrupt Status                 */
#define PORT_IE     0x14    /* Interrupt Enable                 */
#define PORT_CMD    0x18    /* Command and Status               */
#define PORT_TFD    0x20    /* Task File Data                   */
#define PORT_SIG    0x24    /* Signature                        */
#define PORT_SSTS   0x28    /* SATA Status                      */
#define PORT_SCTL   0x2C    /* SATA Control                     */
#define PORT_SERR   0x30    /* SATA Error                       */
#define PORT_SACT   0x34    /* SATA Active                      */
#define PORT_CI     0x38    /* Command Issue                    */

/* ── PORT_CMD bits ────────────────────────────────────────────────────────── */
#define PORT_CMD_ST     (1u << 0)   /* Start                    */
#define PORT_CMD_FRE    (1u << 4)   /* FIS Receive Enable       */
#define PORT_CMD_FR     (1u << 14)  /* FIS Receive Running      */
#define PORT_CMD_CR     (1u << 15)  /* Command List Running     */

/* ── PORT_TFD bits ────────────────────────────────────────────────────────── */
#define PORT_TFD_BSY    (1u << 7)
#define PORT_TFD_DRQ    (1u << 3)
#define PORT_TFD_ERR    (1u << 0)

/* ── PORT_IS bits ─────────────────────────────────────────────────────────── */
#define PORT_IS_TFES    (1u << 30)  /* Task File Error Status (fatal) */

/* ── FIS types ────────────────────────────────────────────────────────────── */
#define FIS_TYPE_REG_H2D        0x27

/* ── ATA commands ─────────────────────────────────────────────────────────── */
#define ATA_CMD_READ_DMA_EX     0x25    /* READ DMA EXT       */
#define ATA_CMD_WRITE_DMA_EX    0x35    /* WRITE DMA EXT      */
#define ATA_CMD_IDENTIFY        0xEC    /* IDENTIFY DEVICE    */
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA    /* FLUSH CACHE EXT (LBA48-safe) */

/* ── Timeout / retry constants ───────────────────────────────────────────── */
#define AHCI_PORT_STOP_RETRIES  500
#define AHCI_CMD_TIMEOUT        10000
#define AHCI_INNER_DELAY        1000
#define AHCI_WRITE_MAX_ATTEMPTS 3

/* ── Packed structures ────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport  : 4;
    uint8_t  rsv0    : 3;
    uint8_t  c       : 1;   /* 1 = command, 0 = control */
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2;
    uint8_t  device;
    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;
    uint16_t count;
    uint8_t  icc;
    uint8_t  control;
    uint32_t rsv1;
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
    uint32_t dba;           /* Data Base Address (low)  */
    uint32_t dbau;          /* Data Base Address (high) */
    uint32_t rsv0;
    uint32_t dbc  : 22;     /* Byte count (0-based)     */
    uint32_t rsv1 : 9;
    uint32_t i    : 1;      /* Interrupt on completion  */
} __attribute__((packed)) hba_prdt_entry_t;

typedef struct {
    uint8_t          cfis[64];      /* Command FIS              */
    uint8_t          acmd[16];      /* ATAPI command (unused)   */
    uint8_t          rsv[48];
    hba_prdt_entry_t prdt_entry[1]; /* PRDT (at least 1 entry)  */
} __attribute__((packed)) hba_cmd_tbl_t;

typedef struct {
    uint8_t  cfl   : 5;     /* Command FIS length in DWORDs */
    uint8_t  a     : 1;     /* ATAPI                        */
    uint8_t  w     : 1;     /* Write (1) / Read (0)         */
    uint8_t  p     : 1;     /* Prefetchable                 */
    uint8_t  r     : 1;     /* Reset                        */
    uint8_t  b     : 1;     /* BIST                         */
    uint8_t  c     : 1;     /* Clear busy on R_OK           */
    uint8_t  rsv0  : 1;
    uint8_t  pmp   : 4;     /* Port Multiplier port         */
    uint16_t prdtl;         /* PRDT entry count             */
    uint32_t prdbc;         /* PRD byte count (HW-written)  */
    uint32_t ctba;          /* Command table base (low)     */
    uint32_t ctbau;         /* Command table base (high)    */
    uint32_t rsv1[4];
} __attribute__((packed)) hba_cmd_header_t;

/* ── Static AHCI driver state ─────────────────────────────────────────────── */
static uint64_t ahci_base        = 0;
static int      ahci_initialized = 0;
static uint8_t  active_port      = 0xFF;

/*
 * DMA-capable memory for AHCI structures.
 * NOTE: In a real OS these must be physical/identity-mapped addresses.
 *       Replace (uint64_t)ptr with your virt_to_phys(ptr) as needed.
 */
static uint8_t cmd_list_mem[1024]       __attribute__((aligned(1024)));
static uint8_t fis_mem[256]             __attribute__((aligned(256)));
static uint8_t cmd_table_mem[32 * 256]  __attribute__((aligned(128)));

/* ── Low-level MMIO helpers ───────────────────────────────────────────────── */

static inline uint32_t ahci_read32(uint64_t base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void ahci_write32(uint64_t base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

/* Portable inner-loop delay (replaces open-coded volatile loops). */
static inline void ahci_delay(void)
{
    for (volatile int i = 0; i < AHCI_INNER_DELAY; i++);
}

/* ── Port control helpers ─────────────────────────────────────────────────── */

static void ahci_port_stop(uint64_t port_base)
{
    uint32_t cmd = ahci_read32(port_base, PORT_CMD);
    cmd &= ~(PORT_CMD_ST | PORT_CMD_FRE);
    ahci_write32(port_base, PORT_CMD, cmd);

    for (int i = 0; i < AHCI_PORT_STOP_RETRIES; i++) {
        cmd = ahci_read32(port_base, PORT_CMD);
        if (!(cmd & (PORT_CMD_CR | PORT_CMD_FR)))
            return;
        ahci_delay();
    }
    LOG_INFO("SATA: port stop timed out (CMD=0x%08X)",
             ahci_read32(port_base, PORT_CMD));
}

static void ahci_port_start(uint64_t port_base)
{
    /* Wait until CR (command list running) is clear before engaging ST */
    for (int i = 0; i < AHCI_PORT_STOP_RETRIES; i++) {
        if (!(ahci_read32(port_base, PORT_CMD) & PORT_CMD_CR))
            break;
        ahci_delay();
    }
    uint32_t cmd = ahci_read32(port_base, PORT_CMD);
    cmd |= PORT_CMD_FRE | PORT_CMD_ST;
    ahci_write32(port_base, PORT_CMD, cmd);
}

/*
 * Stop the port, re-program CLB/FB, clear sticky error bits, then start it.
 * Must be called every time after a port reset so that the hardware has valid
 * descriptor pointers.
 */
static int ahci_port_init(uint8_t port_num)
{
    if (!ahci_base) return -1;

    uint64_t port_base = ahci_base + AHCI_PORT_BASE + (port_num * AHCI_PORT_SIZE);
    uint64_t clb_phys  = (uint64_t)cmd_list_mem;   /* virt_to_phys if needed */
    uint64_t fb_phys   = (uint64_t)fis_mem;

    ahci_port_stop(port_base);

    ahci_write32(port_base, PORT_CLB,  (uint32_t)(clb_phys & 0xFFFFFFFF));
    ahci_write32(port_base, PORT_CLBU, (uint32_t)(clb_phys >> 32));
    ahci_write32(port_base, PORT_FB,   (uint32_t)(fb_phys  & 0xFFFFFFFF));
    ahci_write32(port_base, PORT_FBU,  (uint32_t)(fb_phys  >> 32));

    /* Clear all sticky error and interrupt bits */
    ahci_write32(port_base, PORT_SERR, 0xFFFFFFFF);
    ahci_write32(port_base, PORT_IS,   0xFFFFFFFF);

    ahci_port_start(port_base);
    active_port = port_num;
    return 0;
}

/* ── Command slot helpers ─────────────────────────────────────────────────── */

/** Return index of the first free command slot, or -1 if all are occupied. */
static int ahci_find_free_slot(uint64_t port_base)
{
    uint32_t occupied = ahci_read32(port_base, PORT_SACT) |
                        ahci_read32(port_base, PORT_CI);
    for (int s = 0; s < 32; s++) {
        if (!(occupied & (1u << s)))
            return s;
    }
    return -1;
}

/**
 * Build a command header + command table for the given slot.
 *
 * @param slot      Command slot index (0-31)
 * @param write     1 = host→device data transfer; 0 = device→host or no-data
 * @param prdtl     Number of PRDT entries (0 for commands with no data phase)
 * @param buf       DMA buffer (ignored when prdtl == 0)
 * @param buf_len   Transfer size in bytes (ignored when prdtl == 0)
 * @param cmd       ATA command byte
 * @param lba       48-bit LBA (0 for non-LBA commands)
 * @param count     Sector count (0 for non-data commands)
 */
static void ahci_build_cmd(int        slot,
                            int        write,
                            uint16_t   prdtl,
                            const void *buf,
                            uint32_t   buf_len,
                            uint8_t    cmd,
                            uint64_t   lba,
                            uint16_t   count)
{
    /* ── Command header ── */
    hba_cmd_header_t *hdr = &((hba_cmd_header_t *)cmd_list_mem)[slot];
    memset(hdr, 0, sizeof(*hdr));
    hdr->cfl   = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    hdr->w     = write ? 1 : 0;
    hdr->prdtl = prdtl;

    /* ── Command table ── */
    uint8_t   *tbl_raw  = cmd_table_mem + slot * 256;
    uint64_t   tbl_phys = (uint64_t)tbl_raw;
    memset(tbl_raw, 0, 256);   /* zero the full 256-byte slot, not just sizeof */

    hdr->ctba  = (uint32_t)(tbl_phys & 0xFFFFFFFF);
    hdr->ctbau = (uint32_t)(tbl_phys >> 32);

    hba_cmd_tbl_t *tbl = (hba_cmd_tbl_t *)tbl_raw;

    /* ── PRDT entry ── */
    if (prdtl > 0 && buf) {
        uint64_t buf_phys = (uint64_t)buf;
        tbl->prdt_entry[0].dba  = (uint32_t)(buf_phys & 0xFFFFFFFF);
        tbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
        tbl->prdt_entry[0].dbc  = buf_len - 1;  /* 0-based byte count */
        tbl->prdt_entry[0].i    = 0;
    }

    /* ── Register H2D FIS ── */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c        = 1;          /* command register update */
    fis->command  = cmd;
    fis->device   = 1u << 6;   /* LBA mode */
    fis->count    = count;

    fis->lba0 = (uint8_t)(lba        & 0xFF);
    fis->lba1 = (uint8_t)((lba >>  8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
}

/**
 * Issue the command sitting in `slot` and wait for completion.
 * Clears PORT_IS (W1C) on exit regardless of outcome.
 *
 * @return 0 on success, -1 on Task File Error or timeout.
 */
static int ahci_issue_and_wait(uint64_t port_base, int slot)
{
    ahci_write32(port_base, PORT_IS, 0xFFFFFFFF);   /* clear before issue */
    ahci_write32(port_base, PORT_CI, (1u << slot));

    for (int i = 0; i < AHCI_CMD_TIMEOUT; i++) {
        if (!(ahci_read32(port_base, PORT_CI) & (1u << slot)))
            break;
        ahci_delay();
    }

    uint32_t is = ahci_read32(port_base, PORT_IS);
    if (is) ahci_write32(port_base, PORT_IS, is);   /* W1C – clear consumed bits */

    if (is & PORT_IS_TFES) {
        uint32_t tfd  = ahci_read32(port_base, PORT_TFD);
        uint32_t serr = ahci_read32(port_base, PORT_SERR);
        LOG_ERROR("SATA: command error (IS=0x%08X TFD=0x%08X SERR=0x%08X)",
                  is, tfd, serr);
        ahci_write32(port_base, PORT_SERR, 0xFFFFFFFF);
        return -1;
    }
    return 0;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int sata_detect(void)
{
    int device_count = pcie_device_count();
    LOG_INFO("SATA: scanning PCIe bus (%d devices)", device_count);

    int found_non_ahci = 0;

    for (int i = 0; i < device_count; i++) {
        struct pcie_device dev;
        if (pcie_get_device(i, &dev) != 0)
            continue;

        /* Class 0x01 = mass storage, subclass 0x06 = SATA */
        if (dev.class_code != 0x01 || dev.subclass != 0x06)
            continue;

        sata_controller       = dev;
        sata_controller_found = 1;

        if (dev.prog_if == 0x01) {
            LOG_INFO("SATA: AHCI controller at %02X:%02X.%X",
                     dev.bus, dev.slot, dev.func);
            return 0;
        }
        found_non_ahci = 1;
        LOG_INFO("SATA: non-AHCI SATA controller at %02X:%02X.%X",
                 dev.bus, dev.slot, dev.func);
    }

    if (found_non_ahci) return 2;

    LOG_ERROR("SATA: no controller found");
    return 1;
}

int sata_init(void)
{
    if (!sata_controller_found)
        sata_detect();

    if (!sata_controller_found) {
        LOG_ERROR("SATA: no controller, cannot initialise");
        return -1;
    }

    ahci_base = sata_controller.bar[5];
    if (!ahci_base) {
        LOG_ERROR("SATA: BAR5 not set");
        return -1;
    }

    /*
     * AHCI spec §3.1.2: software must set GHC.AE (bit 31) before using AHCI.
     * On some controllers this bit is hardwired to 1; writing it is harmless.
     */
    uint32_t ghc = ahci_read32(ahci_base, AHCI_GHC_OFFSET);
    if (!(ghc & AHCI_GHC_AE))
        ahci_write32(ahci_base, AHCI_GHC_OFFSET, ghc | AHCI_GHC_AE);

    /* Enable bus mastering + memory-space decode in PCI command register */
    uint32_t pcicmd = pci_config_read32(sata_controller.bus,
                                        sata_controller.slot,
                                        sata_controller.func, 0x04);
    pci_config_write32(sata_controller.bus,
                       sata_controller.slot,
                       sata_controller.func, 0x04, pcicmd | 0x06);

    LOG_INFO("SATA: AHCI base 0x%08X%08X",
             (uint32_t)(ahci_base >> 32), (uint32_t)(ahci_base & 0xFFFFFFFF));

    uint32_t pi = ahci_read32(ahci_base, AHCI_PI_OFFSET);

    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i)))
            continue;

        uint64_t port_base = ahci_base + AHCI_PORT_BASE + (i * AHCI_PORT_SIZE);
        uint8_t  det       = ahci_read32(port_base, PORT_SSTS) & 0xF;

        if (det == 3) {     /* device present, PHY communication established */
            LOG_INFO("SATA: device detected on port %d", i);
            if (ahci_port_init(i) == 0) {
                ahci_initialized = 1;
                return 0;
            }
        }
    }

    LOG_ERROR("SATA: no active device found on any port");
    return -1;
}

int sata_read_sector(uint64_t lba, void *buffer)
{
    if (!ahci_initialized || active_port == 0xFF) {
        LOG_ERROR("SATA: read called before successful init");
        return -1;
    }
    if (!buffer) {
        LOG_ERROR("SATA: NULL buffer passed to sata_read_sector");
        return -1;
    }

    uint64_t port_base = ahci_base + AHCI_PORT_BASE + (active_port * AHCI_PORT_SIZE);

    /* Wait until the port is idle before issuing a new command */
    for (int i = 0; i < 1000; i++) {
        if (!(ahci_read32(port_base, PORT_TFD) & (PORT_TFD_BSY | PORT_TFD_DRQ)))
            break;
        ahci_delay();
    }

    int slot = ahci_find_free_slot(port_base);
    if (slot == -1) {
        LOG_ERROR("SATA: no free command slot for read");
        return -1;
    }

    ahci_build_cmd(slot, /*write=*/0, /*prdtl=*/1, buffer, 512,
                   ATA_CMD_READ_DMA_EX, lba, /*count=*/1);

    return ahci_issue_and_wait(port_base, slot);
}

int sata_write_sector(uint64_t lba, const void *buffer)
{
    if (!ahci_initialized || active_port == 0xFF) {
        LOG_ERROR("SATA: write called before successful init");
        return -1;
    }
    if (!buffer) {
        LOG_ERROR("SATA: NULL buffer passed to sata_write_sector");
        return -1;
    }

    uint64_t port_base = ahci_base + AHCI_PORT_BASE + (active_port * AHCI_PORT_SIZE);

    /* Wait until the port is idle */
    for (int i = 0; i < 1000; i++) {
        if (!(ahci_read32(port_base, PORT_TFD) & (PORT_TFD_BSY | PORT_TFD_DRQ)))
            break;
        ahci_delay();
    }

    int slot = ahci_find_free_slot(port_base);
    if (slot == -1) {
        LOG_ERROR("SATA: no free command slot for write");
        return -1;
    }

    for (int attempt = 1; attempt <= AHCI_WRITE_MAX_ATTEMPTS; attempt++) {

        ahci_build_cmd(slot, /*write=*/1, /*prdtl=*/1, buffer, 512,
                       ATA_CMD_WRITE_DMA_EX, lba, /*count=*/1);

        if (ahci_issue_and_wait(port_base, slot) == 0)
            return 0;

        if (attempt < AHCI_WRITE_MAX_ATTEMPTS) {
            LOG_INFO("SATA: write attempt %d failed, resetting port", attempt);

            /*
             * Full port soft-reset: stop → clear errors → re-program CLB/FB
             * → start.  ahci_port_init() already does all of this.
             */
            ahci_port_init(active_port);

            /* Small settle delay before the next attempt */
            for (volatile int z = 0; z < 200000; z++);
        }
    }

    LOG_ERROR("SATA: write to LBA 0x%08X%08X failed after %d attempts",
              (uint32_t)(lba >> 32), (uint32_t)(lba & 0xFFFFFFFF),
              AHCI_WRITE_MAX_ATTEMPTS);
    return -1;
}

int sata_get_num_blocks(uint64_t *out_blocks)
{
    if (!ahci_initialized || active_port == 0xFF) {
        LOG_ERROR("SATA: IDENTIFY called before successful init");
        return -1;
    }

    /* Buffer must be 512 bytes, physically contiguous, and 512-byte aligned */
    uint8_t buf[512] __attribute__((aligned(512)));
    memset(buf, 0, sizeof(buf));

    uint64_t port_base = ahci_base + AHCI_PORT_BASE + (active_port * AHCI_PORT_SIZE);

    int slot = ahci_find_free_slot(port_base);
    if (slot == -1) {
        LOG_ERROR("SATA: no free command slot for IDENTIFY");
        return -1;
    }

    ahci_build_cmd(slot, /*write=*/0, /*prdtl=*/1, buf, 512,
                   ATA_CMD_IDENTIFY, /*lba=*/0, /*count=*/0);

    if (ahci_issue_and_wait(port_base, slot) != 0)
        return -1;

    const uint16_t *w = (const uint16_t *)buf;

    /*
     * ATA-8 §7.16.7:
     *   Words 100-103: "Maximum User LBA for 48-bit Address feature set"
     *                  (last addressable sector, so total = value + 1)
     *   Words 60-61:   "Total number of user addressable sectors" (direct count)
     *
     * Prefer LBA48 when the words are non-zero.
     */
    uint64_t lba48_max = ((uint64_t)w[103] << 48) | ((uint64_t)w[102] << 32) |
                         ((uint64_t)w[101] << 16) | (uint64_t)w[100];
    uint64_t lba28_cnt = ((uint32_t)w[61] << 16) | w[60];

    uint64_t total;
    if (lba48_max)
        total = lba48_max + 1;          /* max LBA → sector count */
    else if (lba28_cnt)
        total = lba28_cnt;              /* already a count         */
    else {
        LOG_ERROR("SATA: IDENTIFY returned zero sector count");
        return -1;
    }

    LOG_INFO("SATA: disk capacity = 0x%08X%08X sectors",
             (uint32_t)(total >> 32), (uint32_t)(total & 0xFFFFFFFF));

    if (out_blocks) *out_blocks = total;
    return 0;
}

int sata_flush_cache(void)
{
    if (!ahci_initialized || active_port == 0xFF)
        return -1;

    uint64_t port_base = ahci_base + AHCI_PORT_BASE + (active_port * AHCI_PORT_SIZE);

    int slot = ahci_find_free_slot(port_base);
    if (slot == -1) {
        LOG_ERROR("SATA: no free command slot for FLUSH");
        return -1;
    }

    /*
     * Use FLUSH CACHE EXT (0xEA) rather than FLUSH CACHE (0xE7).
     * 0xEA is mandatory for LBA48-capable drives and is a superset of 0xE7.
     */
    ahci_build_cmd(slot, /*write=*/0, /*prdtl=*/0, /*buf=*/NULL, /*len=*/0,
                   ATA_CMD_FLUSH_CACHE_EXT, /*lba=*/0, /*count=*/0);

    if (ahci_issue_and_wait(port_base, slot) != 0) {
        LOG_ERROR("SATA: FLUSH CACHE EXT failed");
        return -1;
    }
    return 0;
}

int sata_is_present(void)
{
    return ahci_initialized;
}