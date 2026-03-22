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
#ifndef KERNEL_GRAPHICS_H
#define KERNEL_GRAPHICS_H

#include "../include/efi.h"
#include <stdint.h>

void init_backbuffer(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
void blit_backbuffer(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
void clear_screen_fb(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, uint8_t r, uint8_t g, uint8_t b);
void draw_box(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t color);
void put_pixel(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, uint32_t x, uint32_t y, uint32_t color);
uint32_t pack_pixel(EFI_GRAPHICS_PIXEL_FORMAT fmt, uint8_t r, uint8_t g, uint8_t b);
/* Fill a row of pixels (used for fast fills and clearing) */
void fill_row_pixels(unsigned int *dst, unsigned int color, size_t n);

/* backbuffer accessors */
unsigned int *get_backbuffer_ptr(void);
unsigned int get_backbuf_width(void);
unsigned int get_backbuf_height(void);

/* Backbuffer dirty tracking — mark when backbuffer contents change so the
 * compositor can skip pointless full-screen blits when nothing has changed. */
void mark_frame_dirty(void);
int is_frame_dirty(void);
void clear_frame_dirty(void);

void draw_box_rgba(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
                   uint32_t x0, uint32_t y0,
                   uint32_t w, uint32_t h,
                   uint32_t rgba);

#endif // KERNEL_GRAPHICS_H