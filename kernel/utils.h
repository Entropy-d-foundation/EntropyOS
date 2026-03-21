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
#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include "../include/efi.h"

void refresh(uint64_t ms, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
/* Refresh for a target number of TSC ticks (high-resolution frame pacing) */
void refresh_frame_ticks(uint64_t target_ticks, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
void big_fault(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, const char *msg);

#endif // KERNEL_UTILS_H