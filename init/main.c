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
#include "../include/efi.h"
#include "../init/main.h"
#include "../kernel/time.h"
#include "../kernel/boot.h"
#include "../gui/graphics_init.h"
#include "../drivers/poweroffreboot.h"
#include "../drivers/pci/pci.h"
#include "../drivers/pcie/pcie.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/thumbdrive/mass_storage.h"
#include "../drivers/ps2/touchpad.h"
#include "../scheduler/scheduler.h"
#include "../drivers/sata/sata.h"
#include "../fs/fat32/fat32.h"
#include "../fs/exfat/exfat.h"
#include "../fs/gpt/gpt.h"
#include "../include/debug_serial.h"

/* Embedded BOOTX64.EFI blob generated at build time */
extern const uint8_t BOOTX64_EFI[];
extern const uint32_t BOOTX64_EFI_SIZE;

/* Callback for listing FAT32 files */
static void
list_file_callback(const char *name, uint32_t size, uint8_t is_dir)
{
        text("FAT32: ");
        if (is_dir)
                text("[DIR]  ");
        else
                text("[FILE] ");
        text((char *)name);
        text("\n");
}

/* Callback for listing ExFAT files */
static void
list_exfat_callback(const char *name, uint64_t size, uint8_t is_dir)
{
        text("ExFAT: ");
        if (is_dir)
                text("[DIR]  ");
        else
                text("[FILE] ");
        text((char *)name);
        text("\n");
}

/* Callback for listing GPT partitions */
static void
list_partition_callback(uint32_t index, const gpt_partition_info_t *info)
{
        text("GPT: Partition ");
        char idx_str[12];
        uint32_t val = index;
        int pos = 0;
        
        if (val == 0) {
                idx_str[pos++] = '0';
        } else {
                char temp[12];
                int temp_pos = 0;
                while (val > 0) {
                        temp[temp_pos++] = '0' + (val % 10);
                        val /= 10;
                }
                while (temp_pos > 0) {
                        idx_str[pos++] = temp[--temp_pos];
                }
        }
        idx_str[pos] = '\0';
        
        text(idx_str);
        text(": ");
        text(info->name);
        text("\n");
}

void
kernel_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
        uint8_t delay = 15; /* Milliseconds */

        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = locateGOP(SystemTable);
        g_gop = gop;

        /* Prefer 1900x1080 mode when GOP is available. If exact mode isn't
         * available, select the closest match by Euclidean distance. Perform
         * this before exiting UEFI so SetMode/QueryMode are callable. */
        if (gop && gop->Mode) {
                typedef EFI_STATUS (EFIAPI *efi_query_mode_t)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
                typedef EFI_STATUS (EFIAPI *efi_set_mode_t)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);

                efi_query_mode_t query_mode = (efi_query_mode_t)gop->QueryMode;
                efi_set_mode_t set_mode = (efi_set_mode_t)gop->SetMode;

                UINT32 best_mode = (UINT32)-1;
                double best_score = -1.0;
                const UINT32 want_w = 1900, want_h = 1080;

                for (UINT32 m = 0; m < gop->Mode->MaxMode; ++m) {
                        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
                        UINTN size = 0;
                        if (query_mode && query_mode(gop, m, &size, &info) == EFI_SUCCESS && info) {
                                if (info->HorizontalResolution == want_w && info->VerticalResolution == want_h) {
                                        best_mode = m; best_score = 0.0; break;
                                }
                                /* score = squared distance to desired resolution */
                                double dw = (double)info->HorizontalResolution - (double)want_w;
                                double dh = (double)info->VerticalResolution - (double)want_h;
                                double score = dw*dw + dh*dh;
                                if (best_score < 0.0 || score < best_score) {
                                        best_score = score;
                                        best_mode = m;
                                }
                        }
                }

                if (best_mode != (UINT32)-1 && set_mode) {
                        if (set_mode(gop, best_mode) == EFI_SUCCESS) {
                                serial_printf("GOP: screen resolution set to ");
                                /* print resolution without stdio */
                                char buf[32];
                                EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = gop->Mode->Info;
                                int w = (int)mi->HorizontalResolution; int h = (int)mi->VerticalResolution;
                                if (w == 0) {
                                        serial_printf("(unknown)\n");
                                } else {
                                        /* format "<w>x<h>\n" into buf */
                                        int p = 0;
                                        /* write width */
                                        int v = w; char tmp[12]; int tp = 0;
                                        if (v == 0) tmp[tp++] = '0';
                                        while (v > 0 && tp < (int)sizeof(tmp)) { tmp[tp++] = '0' + (v % 10); v /= 10; }
                                        while (tp > 0) buf[p++] = tmp[--tp];
                                        buf[p++] = 'x';
                                        /* write height */
                                        v = h; tp = 0;
                                        if (v == 0) tmp[tp++] = '0';
                                        while (v > 0 && tp < (int)sizeof(tmp)) { tmp[tp++] = '0' + (v % 10); v /= 10; }
                                        while (tp > 0) buf[p++] = tmp[--tp];
                                        buf[p++] = '\n';
                                        buf[p] = '\0';
                                        serial_printf("%s", buf);
                                }
                        } else {
                                serial_printf("ERROR: GOP: SetMode failed\n");
                        }
                }
        }

        calibrate_tsc((void *)SystemTable);
        wait(delay);
        byebyeUEFI(ImageHandle, SystemTable, gop);

        /* Initializing Hardware Drivers*/

        pci_init();
        serial_printf("Isn't needed to init PCI as it's already there\n");
        wait(delay);
        if (pcie_init() == 0) {
                serial_printf("PCIe: initialized successfully\n");
        } else {
                serial_printf("FAILED: Could not do anything with PCIe (maybe absent on this hardware)\n");
        }

        wait(delay);
        if (xhci_init() == 0) {
                serial_printf("xHCI: initialized successfully\n");
        } else {
                serial_printf("FAIL FAIL FAIL: xHCI initialization failed (Something Ain't right with xHCI)\n");
        }
        wait(delay);

        if (xhci_is_present()) {
                wait(delay);
                ms_init();
        }

        wait(delay);
        scheduler_init();

        wait(delay);
        int touchpad_status = usb_touchpad_init();

        if (touchpad_status != 0) {
                wait(delay);
                ps2_touchpad_init();
        }

        g_gop = gop;
        wait(delay);

        graphics_init_and_run();

        wait(delay);
        shutdown();
}