// SPDX-License-Identifier: GPL-3.0
#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include "../include/efi.h"
/* Single-call GUI text helper (simple): */
void text(const char *s);

/* Advanced position-aware renderer (internal) */
void text_with_pos(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINTN col, UINTN row,
        UINT8 fg, UINT8 bg, const char *s);

/* Render stored log history to the given GOP (drawn by UI loop) */
void console_render_history(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

#endif // KERNEL_CONSOLE_H