/* SPDX-License-Identifier: GPL-3.0 */
/*
 * EntropyOS/kernel/boot.h
 *
 * include file for stuff that's used on boot by kernel
 *
 * Copyright (C) 2026 Gabriel Sîrbu
 */
#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include "../include/efi.h"

EFI_GRAPHICS_OUTPUT_PROTOCOL *locateGOP(EFI_SYSTEM_TABLE *SystemTable);
void byebyeUEFI(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

#endif // KERNEL_BOOT_H
