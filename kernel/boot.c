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
#include "boot.h"
#include "debug_serial.h"

EFI_GRAPHICS_OUTPUT_PROTOCOL *locateGOP(EFI_SYSTEM_TABLE *SystemTable)
{
            if (!SystemTable)
            {
                    return NULL;
            }
            
            EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
            EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
            if (!bs) return NULL;

            typedef EFI_STATUS (EFIAPI *locate_protocol_fn)(const void*,
                    void*, void**);

            typedef EFI_STATUS (EFIAPI *locate_handle_buffer_fn)(UINTN,
                    const void*, void*, UINTN*, EFI_HANDLE**);

            typedef EFI_STATUS (EFIAPI *handle_protocol_fn)
                        (EFI_HANDLE, const void*, void**);

        locate_protocol_fn locate_protocol =
                        (locate_protocol_fn)bs->LocateProtocol;
        if (locate_protocol) {
                EFI_STATUS rc = locate_protocol((const void*)
                        &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID
                        , NULL, (void**)&gop);
                if (rc == 0 && gop) return gop;
        }

            locate_handle_buffer_fn lhb = (locate_handle_buffer_fn)bs->
                        LocateHandleBuffer;
            handle_protocol_fn hp = (handle_protocol_fn)bs->HandleProtocol;
        if (lhb && hp) {
                UINTN no = 0; EFI_HANDLE *handles = NULL;
                EFI_STATUS rc = lhb(2 /*ByProtocol*/,
                        (const void*)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                        NULL, &no, &handles);
        if (rc == 0 && no > 0 && handles) {
                for (UINTN i = 0; i < no; ++i) {
                        EFI_STATUS hr = hp(handles[i],
                                (const void*)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                                (void**)&gop);
                        if (hr == 0 && gop) return gop;
                }
        }
    }

        if (lhb && hp) {
                UINTN no2 = 0; EFI_HANDLE *h2 = NULL;
                EFI_STATUS rc2 = lhb(0 /*AllHandles*/, NULL, NULL, &no2, &h2);
                if (rc2 == 0 && no2 > 0 && h2) {
                        for (UINTN i = 0; i < no2; ++i) {
                        EFI_STATUS hr = hp(h2[i],
                                (const void*)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                                (void**)&gop);
                        if (hr == 0 && gop) return gop;
                }
            }
        }

        return NULL;
}

void byebyeUEFI(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
        if (gop && SystemTable && SystemTable->BootServices)
        {
                typedef EFI_STATUS (EFIAPI *get_memory_map_fn)(UINTN *MemoryMapSize,
                                void *MemoryMap, UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion);
                typedef EFI_STATUS (EFIAPI *exit_boot_services_fn)(EFI_HANDLE ImageHandle, UINTN MapKey);

                get_memory_map_fn get_memory_map = (get_memory_map_fn)SystemTable->BootServices->GetMemoryMap;
                exit_boot_services_fn exit_boot_services = (exit_boot_services_fn)SystemTable->BootServices->ExitBootServices;

                if (get_memory_map && exit_boot_services)
                {
                        // Try to get the memory map size
                        UINTN map_size = 0;
                        UINTN map_key = 0;
                        UINTN desc_size = 0;
                        unsigned int desc_ver = 0;
                        EFI_STATUS rc = get_memory_map(&map_size, NULL, &map_key, &desc_size, &desc_ver);
                        // EFI commonly returns EFI_BUFFER_TOO_SMALL; allocate buffer accordingly
                        if (rc != EFI_SUCCESS)
                        {
                                        // add some extra space for safety
                                        map_size += 4096;
                                        static uint8_t memmap_buf[65536];
                                        void *memmap = memmap_buf;
                                        if (map_size > sizeof(memmap_buf)) memmap = memmap_buf; // truncated but attempt

                                        rc = get_memory_map(&map_size, memmap, &map_key, &desc_size, &desc_ver);
                                if (rc == EFI_SUCCESS)
                                {
                                        // Call ExitBootServices with retrieved map key
                                        EFI_STATUS xr = exit_boot_services(ImageHandle, map_key);
                                        if (xr == EFI_SUCCESS) {
                                                LOG_INFO("Exiting UEFI boot services succeeded");
                                        } else {
                                                LOG_ERROR("ExitBootServices returned error: 0x%x", (unsigned)xr);
                                        }
                                }
                        }
                        else
                        {
                                // map size was zero but success; still attempt exit
                                EFI_STATUS xr = exit_boot_services(ImageHandle, map_key);
                                (void)xr;
                        }
                }
        }
}