/* MOVED — real UEFI bootloader relocated to `boot/Uefi/bootloader.c` */

/* This stub remains for history/compatibility only. Remove when ready. */


static EFI_SYSTEM_TABLE *gST = NULL;

static void uprint(const char *s)
{
    /* Route non-GUI/Uefi text to serial */
    if (!s) return;
    serial_print(s);
}

/* Callback used to print filenames when listing exFAT root dir */
static void bootloader_exfat_list_cb(const char *name, uint64_t size, uint8_t is_dir)
{
    (void)size; (void)is_dir;
    uprint(name);
    uprint("\r\n");
}

/* Boot blob (iso/BOOT.EFI) is embedded at build time into the installer/bootloader
 * so the bootloader can write BOOTX64.EFI and ENTROPY.OS to target media when
 * performing a forced install. */
extern const uint8_t _binary_iso_BOOT_EFI_start[];
extern const uint8_t _binary_iso_BOOT_EFI_end[];
static const uint8_t *BOOT_EFI = _binary_iso_BOOT_EFI_start;
static uint32_t BOOT_EFI_SIZE = 0; /* initialized at runtime in efi_main */

/* Small helper to zero LBA 0..33 (protective MBR + GPT headers/entries) */
static int wipe_disk_headers_bl(void)
{
    if (!block_is_present()) return -1;
    uint8_t zero_sector[512]; for (int i = 0; i < 512; ++i) zero_sector[i] = 0;
    for (uint64_t lba = 0; lba <= 33; ++lba) {
        if (block_write_sector(lba, zero_sector) != 0) return -1;
    }
    return 0;
}

/* Perform a forced install on the attached block device: wipe GPT, create BOOT/DATA,
 * format BOOT (FAT32) and DATA (ExFAT) and write BOOTX64.EFI + ENTROPY.OS. */
static int bootloader_force_install(void)
{
    uprint("Bootloader: performing forced reinstall (wipe -> partition -> format)\r\n");

    uint64_t total_sectors = 0;
    if (ms_is_present()) {
        total_sectors = ms_get_num_blocks();
    } else if (sata_is_present()) {
        if (sata_get_num_blocks(&total_sectors) != 0) {
            uprint("Bootloader: unable to read SATA capacity\r\n");
            return -1;
        }
    } else {
        uprint("Bootloader: no writable backend present\r\n");
        return -1;
    }

    /* Wipe headers then create GPT */
    wipe_disk_headers_bl();
    if (gpt_create_table(total_sectors, 0) != 0) {
        uprint("Bootloader: gpt_create_table failed\r\n");
        return -1;
    }

    uint8_t efi_type[]   = GPT_TYPE_EFI_SYSTEM;
    uint8_t data_type[]  = GPT_TYPE_MICROSOFT_BASIC_DATA;
    uint64_t fat_start = 2048;
    uint64_t fat_end = fat_start + (300ULL * 1024ULL * 1024ULL / 512ULL) - 1ULL;
    uint64_t exfat_start = fat_end + 1ULL;
    uint64_t exfat_end = total_sectors - 34ULL;

    if (gpt_add_partition(fat_start, fat_end, efi_type, "BOOT") != 0 ||
        gpt_add_partition(exfat_start, exfat_end, data_type, "DATA") != 0) {
        uprint("Bootloader: gpt_add_partition failed\r\n");
        return -1;
    }

    /* Format FAT32 on partition 0 and write BOOTX64.EFI */
    gpt_partition_info_t pi;
    if (gpt_get_partition(0, &pi) == 0 && pi.valid) {
        uint64_t fat_sectors = pi.last_lba - pi.first_lba + 1ULL;
        fat32_set_partition_offset(pi.first_lba);
        if (fat32_format(fat_sectors, "BOOT") == 0 && fat32_init() == 0) {
            uint32_t root_cluster = fat32_get_root_cluster();
            uint32_t efi_cluster = fat32_find_directory(root_cluster, "EFI");
            if (efi_cluster == 0) {
                fat32_create_directory("EFI");
                efi_cluster = fat32_find_directory(root_cluster, "EFI");
            }
            if (efi_cluster != 0) {
                fat32_create_directory_in(efi_cluster, "BOOT");
                uint32_t boot_cluster = fat32_find_directory(efi_cluster, "BOOT");
                if (boot_cluster != 0) {
                    if (fat32_write_file_in(boot_cluster, "BOOTX64.EFI", BOOT_EFI, BOOT_EFI_SIZE) != 0) {
                        uprint("Bootloader: Failed to write BOOTX64.EFI to FAT32\r\n");
                        /* continue to attempt DATA partition */
                    }
                }
            }
        } else {
            uprint("Bootloader: FAT32 format failed\r\n");
        }
    } else {
        uprint("Bootloader: Failed to get partition 0 info\r\n");
    }

    /* Format ExFAT on partition 1 and write ENTROPY.OS */
    gpt_partition_info_t pi_data;
    if (gpt_get_partition(1, &pi_data) == 0 && pi_data.valid) {
        uint64_t exfat_sectors = pi_data.last_lba - pi_data.first_lba + 1ULL;
        exfat_set_partition_offset(pi_data.first_lba);
        if (exfat_format(exfat_sectors, "DATA") == 0 && exfat_init() == 0) {
            if (exfat_write_file("ENTROPY.OS", BOOT_EFI, BOOT_EFI_SIZE) == 0) {
                uprint("Bootloader: ENTROPY.OS written to DATA partition\r\n");

                /* Force device to flush cache so host image receives writes immediately */
                if (sata_is_present()) {
                    if (sata_flush_cache() != 0) uprint("Bootloader: warning — sata_flush_cache failed\r\n");
                }

                /* Verify written file immediately */
                exfat_file_info_t vinfo; if (exfat_get_file_info("ENTROPY.OS", &vinfo) >= 0 && vinfo.valid) {
                    uprint("Bootloader: verification OK (file present)\r\n");

                    /* Create expected system tree (match installer behavior) */
                    if (exfat_create_dir_path("freedom/user") == 0)
                        uprint("Bootloader: Created /freedom/user\r\n");
                    else
                        uprint("Bootloader: /freedom/user already exists or creation failed\r\n");

                    if (exfat_create_dir_path("sys/DoNotTouch") == 0)
                        uprint("Bootloader: Created /sys/DoNotTouch\r\n");
                    else
                        uprint("Bootloader: /sys/DoNotTouch already exists or creation failed\r\n");

                    if (exfat_create_dir_path("sys/fonts") == 0)
                        uprint("Bootloader: Created /sys/fonts\r\n");
                    else
                        uprint("Bootloader: /sys/fonts already exists or creation failed\r\n");

                    /* Write a copy of ENTROPY.OS into /sys/DoNotTouch/Entropy.OS */
                    exfat_file_info_t sys_info;
                    if (exfat_get_file_info("sys", &sys_info) >= 0 && sys_info.is_directory) {
                        exfat_file_info_t dnt_info;
                        if (exfat_get_file_info_in(sys_info.first_cluster, "DoNotTouch", &dnt_info) >= 0 && dnt_info.is_directory) {
                            if (exfat_write_file_in(dnt_info.first_cluster, "Entropy.OS", BOOT_EFI, BOOT_EFI_SIZE) == 0) {
                                exfat_file_info_t probe;
                                if (exfat_get_file_info_in(dnt_info.first_cluster, "Entropy.OS", &probe) >= 0 && !probe.is_directory && probe.size == BOOT_EFI_SIZE) {
                                    uprint("Bootloader: Copied Entropy.OS into /sys/DoNotTouch\r\n");
                                } else {
                                    uprint("Bootloader: Post-write verification failed for /sys/DoNotTouch/Entropy.OS\r\n");
                                }
                            } else {
                                uprint("Bootloader: Failed to write /sys/DoNotTouch/Entropy.OS\r\n");
                            }
                        } else {
                            uprint("Bootloader: cannot locate /sys/DoNotTouch cluster\r\n");
                        }
                    } else {
                        uprint("Bootloader: cannot locate /sys directory after creation\r\n");
                    }

                    /* Place a small placeholder file in /freedom/user (match installer behaviour) */
                    const char *readme = "Welcome user\n";
                    exfat_file_info_t fr_info;
                    if (exfat_get_file_info("freedom", &fr_info) >= 0 && fr_info.is_directory) {
                        exfat_file_info_t user_info;
                        if (exfat_get_file_info_in(fr_info.first_cluster, "user", &user_info) >= 0 && user_info.is_directory) {
                            if (exfat_write_file_in(user_info.first_cluster, "README.txt", readme, (uint32_t)(sizeof("Welcome user\n") - 1)) == 0) {
                                exfat_file_info_t rprobe;
                                if (exfat_get_file_info_in(user_info.first_cluster, "README.txt", &rprobe) >= 0 && !rprobe.is_directory) {
                                    uprint("Bootloader: /freedom/user/README.txt written\r\n");
                                } else {
                                    uprint("Bootloader: /freedom/user/README.txt verification failed\r\n");
                                }
                            } else {
                                uprint("Bootloader: Failed to write /freedom/user/README.txt\r\n");
                            }
                        }
                    }

                    /* Run lightweight fsck on ExFAT partition */
                    if (exfat_fsck() == 0) {
                        uprint("Bootloader: exfat_fsck passed\r\n");
                    } else {
                        uprint("Bootloader: exfat_fsck failed\r\n");
                    }

                } else {
                    uprint("Bootloader: verification FAILED after write\r\n");
                    return -1;
                }
            } else {
                uprint("Bootloader: Failed to write ENTROPY.OS to DATA partition\r\n");
                return -1;
            }
        } else {
            uprint("Bootloader: ExFAT format/init failed for DATA partition\r\n");
            return -1;
        }
    } else {
        uprint("Bootloader: Failed to get DATA partition info (index 1) after GPT create\r\n");
        return -1;
    }

    uprint("Bootloader: forced install complete\r\n");
    uprint("Installer: Done.\n");  /* Signal test completion when forced install finishes */
    return 0;
}

/* Provide a minimal console helper used by filesystem/drivers for logging.
 * Other subsystems call `text()`; provide it here so the bootloader can
 * link without pulling in full GUI/console code. */
void text(const char *s)
{
    uprint(s);
}

static void waitms(uint64_t ms)
{
    if (gST && gST->BootServices && ms > 0) {
        /* Stall expects microseconds */
        typedef VOID (EFIAPI *efi_stall_t)(UINTN);
        efi_stall_t stall = (efi_stall_t)gST->BootServices->Stall;
        if (stall) stall((UINTN)(ms * 1000ULL));
    } else {
        for (volatile uint64_t i = 0; i < ms * 1000ULL; ++i) ;
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;
    gST = SystemTable;
    if (gST && gST->ConOut) gST->ConOut->ClearScreen(gST->ConOut);

    /* initialize embedded blob size */
    BOOT_EFI_SIZE = (uint32_t)(_binary_iso_BOOT_EFI_end - _binary_iso_BOOT_EFI_start);

    uprint("Bootloader: starting\r\n");

    /* Minimal hardware init (reuse same subsystems as installer/kernel expects) */
    extern void pci_init(void);
    extern int pcie_init(void);
    extern int xhci_init(void);
    extern int ms_init(void);
    extern int sata_init(void);
    pci_init(); waitms(1);
    (void)pcie_init(); waitms(1);
    (void)xhci_init(); waitms(1);
    (void)ms_init(); waitms(10);
    (void)sata_init(); waitms(10);

    if (!block_is_present()) {
        uprint("Bootloader: no block device present\r\n");
        return EFI_SUCCESS;
    }

    /* Find GPT partition index 1 (DATA) created by installer and mount exFAT there */
    gpt_partition_info_t pi;
    if (gpt_get_partition(1, &pi) != 0 || !pi.valid) {
        uprint("Bootloader: DATA partition missing — attempting forced reinstall\r\n");
        if (bootloader_force_install() != 0) {
            uprint("Bootloader: forced install failed — cannot continue\r\n");
            return EFI_SUCCESS;
        }
        /* after forced install, refresh GPT state and re-read partition info */
        if (gpt_init() != 0 || gpt_get_partition(1, &pi) != 0 || !pi.valid) {
            uprint("Bootloader: still cannot locate DATA partition after forced install\r\n");
            return EFI_SUCCESS;
        }
    }

    /* Set partition offset for ExFAT driver and init */
    exfat_set_partition_offset(pi.first_lba);
    if (exfat_init() != 0) {
        uprint("Bootloader: exFAT init failed\r\n");
        return EFI_SUCCESS;
    }

    /* Look for ENTROPY.OS in root and load it */
    const char *kernel_name = "ENTROPY.OS";
    exfat_file_info_t info;
    /* Try to find the kernel file; if missing, retry + list directory for debug */
    int retries = 3; int found = 0;
    for (int attempt = 0; attempt < retries; ++attempt) {
        if (exfat_get_file_info(kernel_name, &info) >= 0 && info.valid) { found = 1; break; }
        uprint("Bootloader: ENTROPY.OS not found — retrying\r\n");
        /* List files to aid debugging */
        exfat_list_files(bootloader_exfat_list_cb);
        /* small stall */
        typedef VOID (EFIAPI *efi_stall_t)(UINTN);
        efi_stall_t stall = (efi_stall_t)gST->BootServices->Stall;
        if (stall) stall(100000);
    }
    if (!found) {
        uprint("Bootloader: kernel file ENTROPY.OS not found on DATA partition\r\n");
        return EFI_SUCCESS;
    }

    /* Allocate a buffer via BootServices->AllocatePool (fallback to stack if not available). */
    void *buf = NULL;
    UINTN size = (UINTN)info.size;
    typedef EFI_STATUS (EFIAPI *efi_allocate_pool_t)(UINTN, void **);
    efi_allocate_pool_t alloc = (efi_allocate_pool_t)gST->BootServices->AllocatePool;
    if (alloc && alloc(size, &buf) == EFI_SUCCESS && buf) {
        /* OK: buffer allocated */
    } else {
        /* last-resort: allocate from a static area if the image is small */
        static uint8_t small_buf[4 * 1024 * 1024]; /* 4MB */
        if (size <= sizeof(small_buf)) {
            buf = small_buf;
        } else {
            uprint("Bootloader: cannot allocate buffer for kernel\r\n");
            return EFI_SUCCESS;
        }
    }

    uint32_t bytes_read = 0;
    if (exfat_read_file(kernel_name, buf, (uint32_t)size, &bytes_read) != 0 || bytes_read == 0) {
        uprint("Bootloader: failed to read kernel from ExFAT\r\n");
        return EFI_SUCCESS;
    }

    /* Load image from memory buffer and start it */
    typedef EFI_STATUS (EFIAPI *efi_load_image_t)(UINT8, EFI_HANDLE, void *, void *, UINTN, EFI_HANDLE *);
    typedef EFI_STATUS (EFIAPI *efi_start_image_t)(EFI_HANDLE, UINTN*, CHAR16**);
    efi_load_image_t load_image = (efi_load_image_t)gST->BootServices->LoadImage;
    efi_start_image_t start_image = (efi_start_image_t)gST->BootServices->StartImage;

    if (!load_image || !start_image) {
        uprint("Bootloader: LoadImage/StartImage not available\r\n");
        return EFI_SUCCESS;
    }

    EFI_HANDLE kernel_image = NULL;
    if (load_image(0 /*BootPolicy=false*/, ImageHandle, NULL, buf, (UINTN)bytes_read, &kernel_image) != EFI_SUCCESS) {
        uprint("Bootloader: LoadImage failed\r\n");
        return EFI_SUCCESS;
    }

    uprint("Bootloader: starting kernel image...\r\n");
    UINTN exit_data_size = 0; CHAR16 *exit_data = NULL;
    if (start_image(kernel_image, &exit_data_size, &exit_data) != EFI_SUCCESS) {
        uprint("Bootloader: StartImage failed\r\n");
        return EFI_SUCCESS;
    }

    /* Should not return under normal boot; if it does, hang */
    for (;;) __asm__ ("hlt");
    return EFI_SUCCESS;
}
