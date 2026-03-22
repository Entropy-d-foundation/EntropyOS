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