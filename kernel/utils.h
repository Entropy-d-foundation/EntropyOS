// SPDX-License-Identifier: GPL-3.0
#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include "../include/efi.h"

void refresh(uint64_t ms, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
/* Refresh for a target number of TSC ticks (high-resolution frame pacing) */
void refresh_frame_ticks(uint64_t target_ticks, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
void big_fault(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, const char *msg);

#endif // KERNEL_UTILS_H