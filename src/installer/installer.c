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

#include "../../include/efi.h"
#include "../../kernel/kernel.h"
#include "../../drivers/thumbdrive/mass_storage.h"
#include "../../drivers/block/block.h"
#include "../../drivers/sata/sata.h"
#include "../../fs/gpt/gpt.h"
#include "../../fs/fat32/fat32.h"
#include "../../fs/exfat/exfat.h"
#include "../../kernel/console.h"
/* Enable logging (serial) only for the installer so logs appear on both
 * the UEFI console (via text/uprint) and on the serial debug console. */
#define ENABLE_INSTALLER_LOGS
#include "../../include/debug_serial.h"
#include <string.h>
#include <stdint.h>

/* Minimal installer-local helpers to avoid pulling large GUI/kernel subsystems.
 * Implement `text()` to print to the serial debug console (non-GUI builds).
 * Implement `wait()` using BootServices->Stall when possible.
 */
static EFI_SYSTEM_TABLE *gST = NULL;

void text(const char *s)
{
    /* Output to both serial and UEFI console */
    if (!s) return;
    serial_print(s);
    
    /* Also output to UEFI console if available */
    if (gST && gST->ConOut) {
        /* Convert ASCII to UTF-16 on the stack (max 256 chars) */
        CHAR16 buf[256] = {0};
        int i = 0;
        while (*s && i < (int)sizeof(buf)/2 - 1) {
            buf[i++] = (CHAR16)(*s++);
        }
        buf[i] = 0;
        gST->ConOut->OutputString(gST->ConOut, buf);
    }
}

void wait(uint64_t ms)
{
    if (gST && gST->BootServices && ms > 0) {
        /* Stall expects microseconds */
        uint64_t usec = ms * 1000ULL;
        gST->BootServices->Stall((UINTN)usec);
    } else {
        for (volatile uint64_t i = 0; i < ms * 1000ULL; i++)
            ;
    }
}

/* Wipe the first sectors (protective MBR + GPT headers/entries) to force
 * a clean partition table before re-creating partitions. Returns 0 on
 * success, non-zero on failure. */
static int wipe_disk_headers(void)
{
    if (!block_is_present()) return -1;
    uint8_t zero_sector[512];
    for (int i = 0; i < 512; ++i) zero_sector[i] = 0;

    /* Zero LBA 0..33 (protective MBR + GPT header + partition array area) */
    for (uint64_t lba = 0; lba <= 33; ++lba) {
        if (block_write_sector(lba, zero_sector) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Embedded BOOT.EFI via objcopy binary symbols (see Makefile rules) */
extern const uint8_t _binary_iso_BOOT_EFI_start[];
extern const uint8_t _binary_iso_BOOT_EFI_end[];
static const uint8_t *BOOT_EFI = _binary_iso_BOOT_EFI_start;
static uint32_t BOOT_EFI_SIZE;

/* Simple helper to print ASCII strings via UEFI console (if available) */
static void uprint(const char *s)
{
    /* The kernel's text() expects char*, but this app runs before kernel_main.
     * Here we call the UEFI text protocol if available; fallback to nothing.
     * To keep code simple and small, re-use the kernel text() if linked in.
     */
    extern void text(const char *s);
    text(s);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;

    /* Store SystemTable for use by helpers and clear screen */
    gST = SystemTable;
    if (gST && gST->ConOut)
        gST->ConOut->ClearScreen(gST->ConOut);

    uprint("Installer: Starting\r\n");

    /* Compute embedded BOOT.EFI size from linker-provided binary symbols */
    BOOT_EFI_SIZE = (uint32_t)(_binary_iso_BOOT_EFI_end - _binary_iso_BOOT_EFI_start);

    /* Initialize minimal device subsystems used by installer (PCI, xHCI) */
    extern void pci_init(void);
    extern void pcie_init(void);
    extern void xhci_init(void);
    extern void wait(uint64_t ms);

    pci_init(); wait(1);
    pcie_init(); wait(1);
    xhci_init(); wait(1);

    /* Initialize both possible backends (USB mass-storage and SATA) and check for presence */
    ms_init(); wait(50);
    sata_init(); wait(50);

    if (!block_is_present()) {
        uprint("Installer: No mass-storage device found. Aborting installer.\r\n");
        return EFI_SUCCESS;
    }

    uint64_t total_sectors = 0;
    if (ms_is_present()) {
        total_sectors = ms_get_num_blocks();
    } else if (sata_is_present()) {
        if (sata_get_num_blocks(&total_sectors) != 0) {
            uprint("Installer: Failed to get SATA capacity. Aborting.\r\n");
            return EFI_SUCCESS;
        }
    } else {
        uprint("Installer: No supported block backend found. Aborting.\r\n");
        return EFI_SUCCESS;
    }
    char buf[128];
    /* Force-wipe partition table / headers, then create GPT with two partitions: BOOT (FAT32) and DATA (ExFAT). */
    uprint("Installer: force-wiping partition table & headers\n");
    if (wipe_disk_headers() != 0) {
        uprint("Installer: wipe failed (continuing to attempt GPT recreate)\n");
    }

    if (gpt_create_table(total_sectors, 0) != 0) {
        uprint("Installer: GPT creation failed\n");
        return EFI_SUCCESS;
    }

    uint8_t efi_type[]   = GPT_TYPE_EFI_SYSTEM;
    uint8_t data_type[]  = GPT_TYPE_MICROSOFT_BASIC_DATA;
    uint64_t fat_start = 2048;
    uint64_t fat_end = fat_start + (300ULL * 1024ULL * 1024ULL / 512ULL) - 1ULL;
    uint64_t exfat_start = fat_end + 1ULL;
    uint64_t exfat_end = total_sectors - 34ULL;

    if (gpt_add_partition(fat_start, fat_end, efi_type, "BOOT") != 0 ||
        gpt_add_partition(exfat_start, exfat_end, data_type, "DATA") != 0) {
        uprint("Installer: Failed to add partitions\n");
        return EFI_SUCCESS;
    }

    uprint("Installer: Partitions created\n");

    /* Format FAT32 on partition 0 and write BOOTX64.EFI in EFI/BOOT */
    gpt_partition_info_t pi;
    if (gpt_get_partition(0, &pi) == 0) {
        uint64_t fat_sectors = pi.last_lba - pi.first_lba + 1ULL;
        fat32_set_partition_offset(pi.first_lba);

        if (fat32_format(fat_sectors, "BOOT") == 0 && fat32_init() == 0) {
            LOG_INFO("Installer: FAT32 formatted. Preparing to write BOOTX64.EFI (size=%u bytes)...", BOOT_EFI_SIZE);

            /* Create EFI/BOOT directories */
            fat32_create_directory("EFI");
            uint32_t root_cluster = fat32_get_root_cluster();
            LOG_INFO("Installer: root cluster=%u", root_cluster);
            uint32_t efi_cluster = fat32_find_directory(root_cluster, "EFI");
            if (efi_cluster == 0) {
                LOG_ERROR("Installer: Failed to create/find EFI dir");
            } else {
                LOG_INFO("Installer: EFI dir cluster=%u", efi_cluster);
                fat32_create_directory_in(efi_cluster, "BOOT");
                uint32_t boot_cluster = fat32_find_directory(efi_cluster, "BOOT");
                if (boot_cluster == 0) {
                    LOG_ERROR("Installer: Failed to create/find EFI/BOOT dir");
                } else {
                    LOG_INFO("Installer: BOOT dir cluster=%u; writing file...", boot_cluster);
                    if (fat32_write_file_in(boot_cluster, "BOOTX64.EFI", BOOT_EFI, BOOT_EFI_SIZE) == 0) {
                        /* Show final installer result on UEFI console and serial */
                        if (ms_is_present())
                            LOG_INFO("Installer: BOOTX64.EFI written successfully to USB");
                        else if (sata_is_present())
                            LOG_INFO("Installer: BOOTX64.EFI written successfully to SATA");
                        else
                            LOG_INFO("Installer: BOOTX64.EFI written successfully");

                        /* Verify written file size matches embedded BOOT.EFI (basic check) */
                        uint32_t written_size = 0;
                        if (fat32_get_file_size_in(boot_cluster, "BOOTX64.EFI", &written_size) == 0) {
                            if (written_size == BOOT_EFI_SIZE) {
                                LOG_INFO("Installer: Verification succeeded: size matches embedded BOOT.EFI");
                            } else {
                                LOG_ERROR("Installer: Verification failed: size mismatch (got %u, expected %u)", written_size, BOOT_EFI_SIZE);
                            }
                        } else {
                            uprint("Installer: Verification failed: unable to read file size\n");
                        }

                        /* --- prepare ExFAT DATA partition and write ENTROPY.OS (kernel) --- */
                        gpt_partition_info_t pi_data;
                        if (gpt_get_partition(1, &pi_data) == 0 && pi_data.valid) {
                            uint64_t exfat_sectors = pi_data.last_lba - pi_data.first_lba + 1ULL;
                            exfat_set_partition_offset(pi_data.first_lba);
                            if (exfat_format(exfat_sectors, "DATA") == 0 && exfat_init() == 0) {
                                if (exfat_write_file("ENTROPY.OS", BOOT_EFI, BOOT_EFI_SIZE) == 0) {
                                    LOG_INFO("Installer: ENTROPY.OS written to ExFAT DATA partition (size=%u)", BOOT_EFI_SIZE);

                                    /* Create requested system file structure under ExFAT DATA:
                                     * /freedom/user
                                     * /sys/DoNotTouch/Entropy.OS
                                     * /sys/fonts
                                     * Keep ENTROPY.OS at root (bootloader still expects it there).
                                     */
                                    if (exfat_create_dir_path("freedom/user") == 0) {
                                        LOG_INFO("Installer: Created /freedom/user");
                                    } else {
                                        LOG_INFO("Installer: /freedom/user already exists or creation failed");
                                    }

                                    if (exfat_create_dir_path("sys/DoNotTouch") == 0) {
                                        LOG_INFO("Installer: Created /sys/DoNotTouch");
                                    } else {
                                        LOG_INFO("Installer: /sys/DoNotTouch already exists or creation failed");
                                    }

                                    if (exfat_create_dir_path("sys/fonts") == 0) {
                                        LOG_INFO("Installer: Created /sys/fonts");
                                    } else {
                                        LOG_INFO("Installer: /sys/fonts already exists or creation failed");
                                    }

                                    /* Write a copy of the embedded image into sys/DoNotTouch/Entropy.OS */
                                    const char *entropy_path = "sys/DoNotTouch/Entropy.OS";
                                    int write_ok = 0;

                                    /* retry writes with post-write verification */
                                    for (int attempt = 0; attempt < 3; attempt++) {
                                        if (exfat_write_file(entropy_path, BOOT_EFI, BOOT_EFI_SIZE) == 0) {
                                            exfat_file_info_t probe;
                                            if (exfat_get_file_info(entropy_path, &probe) >= 0 &&
                                                !probe.is_directory &&
                                                probe.size == BOOT_EFI_SIZE) {
                                                LOG_INFO("Installer: Copied Entropy.OS into /sys/DoNotTouch (attempt %d)", attempt+1);
                                                write_ok = 1;
                                                break;
                                            } else {
                                                LOG_ERROR("Installer: Post-write verification failed for /sys/DoNotTouch/Entropy.OS (size=%u)", (unsigned)probe.size);
                                            }
                                        } else {
                                            LOG_ERROR("Installer: write attempt %d failed for /sys/DoNotTouch/Entropy.OS", attempt+1);
                                        }
                                    }

                                    if (!write_ok) {
                                        LOG_ERROR("Installer: Failed to write /sys/DoNotTouch/Entropy.OS after retries");
                                    } else {
                                        /* immediate lightweight fsck pass after the critical copy */
                                        if (exfat_fsck() == 0) {
                                            LOG_INFO("Installer: exfat_fsck passed after Entropy.OS write");
                                        } else {
                                            LOG_ERROR("Installer: exfat_fsck failed after Entropy.OS write");
                                        }
                                    }

                                    /* Place a small placeholder file in /freedom/user (verify) */
                                    exfat_file_info_t fr_info;
                                    const char *readme = "Welcome user\n";
                                    if (exfat_get_file_info("freedom", &fr_info) >= 0 && fr_info.is_directory) {
                                        exfat_file_info_t user_info;
                                        if (exfat_get_file_info_in(fr_info.first_cluster, "user", &user_info) >= 0 && user_info.is_directory) {
                                            if (exfat_write_file_in(user_info.first_cluster, "README.txt", readme, (uint32_t)(sizeof("Welcome user\n") - 1)) == 0) {
                                                exfat_file_info_t rprobe;
                                                if (exfat_get_file_info_in(user_info.first_cluster, "README.txt", &rprobe) >= 0 && !rprobe.is_directory) {
                                                    LOG_INFO("Installer: /freedom/user/README.txt written (size=%u)", (unsigned)rprobe.size);
                                                } else {
                                                    LOG_ERROR("Installer: /freedom/user/README.txt verification failed");
                                                }
                                            } else {
                                                LOG_ERROR("Installer: Failed to write /freedom/user/README.txt");
                                            }
                                        }
                                    }

                                    /* Run lightweight fsck on the freshly-created ExFAT partition (MANDATORY) */
                                    if (exfat_fsck() == 0) {
                                        LOG_INFO("Installer: exfat_fsck passed");
                                    } else {
                                        LOG_ERROR("Installer: exfat_fsck failed");
                                    }

                                    /* Ensure host-side persistence (flush device caches) */
                                    if (sata_is_present()) {
                                        if (sata_flush_cache() != 0) LOG_ERROR("Installer: sata_flush_cache failed");
                                    }
                                } else {
                                    LOG_ERROR("Installer: Failed to write ENTROPY.OS to ExFAT partition");
                                }
                            } else {
                                LOG_ERROR("Installer: ExFAT format/init failed for DATA partition");
                            }
                        } else {
                            LOG_ERROR("Installer: Failed to get DATA partition info (index 1)");
                        }

                    } else {
                        if (ms_is_present())
                            uprint("Installer: Failed to write BOOTX64.EFI to USB\n");
                        else if (sata_is_present())
                            uprint("Installer: Failed to write BOOTX64.EFI to SATA\n");
                        else
                            uprint("Installer: Failed to write BOOTX64.EFI\n");
                    }
                }
            }
        } else {
            uprint("Installer: FAT32 format/init failed\n");
        }
    } else {
        uprint("Installer: Failed to get partition 0 info\n");
    }

    uprint("Installer: Done.\n");

    return EFI_SUCCESS;
}
