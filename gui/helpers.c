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
#include "../init/main.h"
#include "helpers.h"
#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Dirty flag — declared early so static helpers can reference it.
 * ------------------------------------------------------------------------- */
static volatile int s_frame_dirty = 1;

/* -------------------------------------------------------------------------
 * row_copy
 * 4x-unrolled 64-bit copy.  Falls back to 32-bit for tail pixels.
 * ------------------------------------------------------------------------- */
static inline void __attribute__((always_inline))
row_copy(unsigned int *dst, const unsigned int *src, size_t n)
{
        size_t i = 0;
        if ((__builtin_expect((((uintptr_t)dst | (uintptr_t)src) & 7) == 0, 1)))
        {
                uint64_t       *d64 = (uint64_t *)dst;
                const uint64_t *s64 = (const uint64_t *)src;
                size_t n64   = n / 2;
                size_t n64x4 = (n64 / 4) * 4;

                for (size_t j = 0; j < n64x4; j += 4) {
                        d64[j]   = s64[j];
                        d64[j+1] = s64[j+1];
                        d64[j+2] = s64[j+2];
                        d64[j+3] = s64[j+3];
                }
                for (size_t j = n64x4; j < n64; ++j)
                        d64[j] = s64[j];
                i = n64 * 2;
        }
        for (; i < n; ++i)
                dst[i] = src[i];
}

/* -------------------------------------------------------------------------
 * fill_row
 * 4x-unrolled 64-bit fill.  Falls back to 32-bit for tail pixels.
 * ------------------------------------------------------------------------- */
static inline void __attribute__((always_inline))
fill_row(unsigned int *dst, unsigned int color, size_t n)
{
        s_frame_dirty = 1;

        size_t i = 0;
        if (__builtin_expect(((uintptr_t)dst & 7) == 0, 1))
        {
                uint64_t pat   = ((uint64_t)color << 32) | color;
                uint64_t *d64  = (uint64_t *)dst;
                size_t n64     = n / 2;
                size_t n64x4   = (n64 / 4) * 4;

                for (size_t j = 0; j < n64x4; j += 4) {
                        d64[j]   = pat;
                        d64[j+1] = pat;
                        d64[j+2] = pat;
                        d64[j+3] = pat;
                }
                for (size_t j = n64x4; j < n64; ++j)
                        d64[j] = pat;
                i = n64 * 2;
        }
        for (; i < n; ++i)
                dst[i] = color;
}

/* -------------------------------------------------------------------------
 * Backbuffer state
 * ------------------------------------------------------------------------- */
#define BACKBUF_MAX_W 1920
#define BACKBUF_MAX_H 1200

static unsigned int  static_backbuffer[BACKBUF_MAX_W * BACKBUF_MAX_H];
static unsigned int *g_backbuffer    = NULL;
static unsigned int  g_backbuf_width = 0;
static unsigned int  g_backbuf_height = 0;

/* -------------------------------------------------------------------------
 * pack_pixel / unpack_pixel_rgb / blend helpers
 * ------------------------------------------------------------------------- */
u32 pack_pixel(EFI_GRAPHICS_PIXEL_FORMAT fmt, u8 r, u8 g, u8 b)
{
        if (fmt == PixelRedGreenBlueReserved8BitPerColor)
                return (u32)r | ((u32)g << 8) | ((u32)b << 16);
        else
                return (u32)b | ((u32)g << 8) | ((u32)r << 16);
}

static inline void __attribute__((always_inline))
unpack_pixel_rgb(EFI_GRAPHICS_PIXEL_FORMAT fmt, u32 px,
                 u8 *r, u8 *g, u8 *b)
{
        if (fmt == PixelRedGreenBlueReserved8BitPerColor) {
                *r = (u8)(px);
                *g = (u8)(px >> 8);
                *b = (u8)(px >> 16);
        } else {
                *b = (u8)(px);
                *g = (u8)(px >> 8);
                *r = (u8)(px >> 16);
        }
}

static inline u8 __attribute__((always_inline))
blend_chan(u8 src, u8 dst, u8 a)
{
        uint32_t x = (uint32_t)src * (uint32_t)a +
                     (uint32_t)dst * (uint32_t)(255 - a);
        return (u8)((x + (x >> 8) + 0x80) >> 8);
}

void __attribute__((always_inline))
put_pixel(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, u32 x, u32 y, u32 color)
{
        if (__builtin_expect(!gop || !gop->Mode || !gop->Mode->Info, 0))
                return;

        unsigned int screen_w = gop->Mode->Info->HorizontalResolution;
        unsigned int screen_h = gop->Mode->Info->VerticalResolution;

        if (__builtin_expect(x >= screen_w || y >= screen_h, 0))
                return;

        if (__builtin_expect(g_backbuffer != NULL &&
                             g_backbuf_width  == screen_w &&
                             g_backbuf_height == screen_h, 1))
        {
                g_backbuffer[y * g_backbuf_width + x] = color;
                s_frame_dirty = 1;
                return;
        }

        unsigned int pitch = gop->Mode->Info->PixelsPerScanLine;
        unsigned int *fb   = (unsigned int *)(uintptr_t)gop->Mode->FrameBufferBase;
        fb[y * pitch + x]  = color;
}

/* -------------------------------------------------------------------------
 * get_pixel (internal)
 * ------------------------------------------------------------------------- */
static inline u32 __attribute__((always_inline))
get_pixel(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, u32 x, u32 y)
{
        if (__builtin_expect(g_backbuffer != NULL &&
                             g_backbuf_width ==
                             gop->Mode->Info->HorizontalResolution, 1))
                return g_backbuffer[y * g_backbuf_width + x];

        unsigned int pitch = gop->Mode->Info->PixelsPerScanLine;
        unsigned int *fb   = (unsigned int *)(uintptr_t)gop->Mode->FrameBufferBase;
        return fb[y * pitch + x];
}

void draw_box(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
              uint32_t x0, u32 y0, u32 w, u32 h, u32 color)
{
        if (__builtin_expect(!gop || !gop->Mode || !gop->Mode->Info, 0))
                return;

        unsigned int sx = gop->Mode->Info->HorizontalResolution;
        unsigned int sy = gop->Mode->Info->VerticalResolution;

        if (x0 >= sx || y0 >= sy)
                return;

        unsigned int x1 = x0 + w; if (x1 > sx) x1 = sx;
        unsigned int y1 = y0 + h; if (y1 > sy) y1 = sy;
        size_t span    = (size_t)(x1 - x0);

        /* Resolve destination once — backbuffer or framebuffer */
        if (__builtin_expect(g_backbuffer != NULL &&
                             g_backbuf_width  == sx &&
                             g_backbuf_height == sy, 1))
        {
                for (unsigned int y = y0; y < y1; ++y)
                        fill_row(g_backbuffer + (size_t)y * g_backbuf_width + x0,
                                 color, span);
                return;
        }

        unsigned int  pitch = gop->Mode->Info->PixelsPerScanLine;
        unsigned int *fb    = (unsigned int *)(uintptr_t)gop->Mode->FrameBufferBase;
        for (unsigned int y = y0; y < y1; ++y)
                fill_row(fb + (size_t)y * pitch + x0, color, span);
}

/* -------------------------------------------------------------------------
 * clear_screen_fb
 * ------------------------------------------------------------------------- */
void clear_screen_fb(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
                     u8 r, u8 g, u8 b)
{
        if (__builtin_expect(!gop || !gop->Mode || !gop->Mode->Info, 0))
                return;

        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;

        if (info->PixelFormat == PixelBltOnly)
                return;

        unsigned int color  = pack_pixel(info->PixelFormat, r, g, b);
        unsigned int width  = info->HorizontalResolution;
        unsigned int height = info->VerticalResolution;

        if (__builtin_expect(g_backbuffer != NULL &&
                             g_backbuf_width  == width &&
                             g_backbuf_height == height, 1))
        {
                for (unsigned int y = 0; y < height; ++y)
                        fill_row(g_backbuffer + (size_t)y * width, color, width);
                return;
        }

        unsigned int  pitch = info->PixelsPerScanLine;
        unsigned int *fb    = (unsigned int *)(uintptr_t)gop->Mode->FrameBufferBase;
        for (unsigned int y = 0; y < height; ++y)
                fill_row(fb + (size_t)y * pitch, color, width);
}

/* -------------------------------------------------------------------------
 * init_backbuffer
 * ------------------------------------------------------------------------- */
void init_backbuffer(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
        if (!gop || !gop->Mode || !gop->Mode->Info)
                return;

        unsigned int w = gop->Mode->Info->HorizontalResolution;
        unsigned int h = gop->Mode->Info->VerticalResolution;

        if (w <= BACKBUF_MAX_W && h <= BACKBUF_MAX_H) {
                g_backbuffer     = static_backbuffer;
                g_backbuf_width  = w;
                g_backbuf_height = h;
        } else {
                g_backbuffer     = NULL;
                g_backbuf_width  = 0;
                g_backbuf_height = 0;
        }
}

/* -------------------------------------------------------------------------
 * blit_backbuffer
 * ------------------------------------------------------------------------- */
void blit_backbuffer(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
        if (__builtin_expect(!g_backbuffer || !gop ||
                             !gop->Mode || !gop->Mode->Info, 0))
                return;

        unsigned int  w     = g_backbuf_width;
        unsigned int  h     = g_backbuf_height;
        unsigned int  pitch = gop->Mode->Info->PixelsPerScanLine;
        unsigned int *fb    = (uint32_t *)(uintptr_t)gop->Mode->FrameBufferBase;

        /* Pitch == width: copy whole buffer in one shot */
        if (__builtin_expect(pitch == w, 1)) {
                row_copy(fb, g_backbuffer, (size_t)w * (size_t)h);
                s_frame_dirty = 0;
                return;
        }

        for (unsigned int y = 0; y < h; ++y)
                row_copy(fb + (size_t)y * pitch,
                         g_backbuffer + (size_t)y * w,
                         (size_t)w);

        s_frame_dirty = 0;
}

/* Inline blend macro — avoids function call overhead in hot loop */
#define BLEND(s, d, a) \
    ({ uint32_t _x = (uint32_t)(s)*(uint32_t)(a) + \
                     (uint32_t)(d)*(uint32_t)(255-(a)); \
       (u8)((_x + (_x >> 8) + 0x80) >> 8); })

void draw_box_rgba(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
                   u32 x0, u32 y0,
                   u32 w,  u32 h,
                   u32 rgba)
{
        if (__builtin_expect(!gop || !gop->Mode || !gop->Mode->Info, 0))
                return;

        u32 sx = gop->Mode->Info->HorizontalResolution;
        u32 sy = gop->Mode->Info->VerticalResolution;

        if (x0 >= sx || y0 >= sy)
                return;

        u32 x1 = x0 + w; if (x1 > sx) x1 = sx;
        u32 y1 = y0 + h; if (y1 > sy) y1 = sy;

        u8 src_r = (u8)(rgba >> 24);
        u8 src_g = (u8)(rgba >> 16);
        u8 src_b = (u8)(rgba >>  8);
        u8 src_a = (u8)(rgba);

        if (src_a == 0)
                return;

        u32  span  = x1 - x0;
        EFI_GRAPHICS_PIXEL_FORMAT fmt = gop->Mode->Info->PixelFormat;

        if (src_a == 255) {
                u32 color = pack_pixel(fmt, src_r, src_g, src_b);

                if (__builtin_expect(g_backbuffer != NULL &&
                                     g_backbuf_width  == sx &&
                                     g_backbuf_height == sy, 1))
                {
                        for (u32 y = y0; y < y1; ++y)
                                fill_row(g_backbuffer +
                                         (size_t)y * g_backbuf_width + x0,
                                         color, span);
                        return;
                }

                unsigned int  pitch = gop->Mode->Info->PixelsPerScanLine;
                unsigned int *fb    = (unsigned int *)(uintptr_t)
                                      gop->Mode->FrameBufferBase;
                for (u32 y = y0; y < y1; ++y)
                        fill_row(fb + (size_t)y * pitch + x0, color, span);
                return;
        }

        unsigned int *base;
        size_t        stride;

        if (__builtin_expect(g_backbuffer != NULL &&
                             g_backbuf_width  == sx &&
                             g_backbuf_height == sy, 1))
        {
                base   = g_backbuffer;
                stride = g_backbuf_width;
        }
        else
        {
                base   = (unsigned int *)(uintptr_t)gop->Mode->FrameBufferBase;
                stride = gop->Mode->Info->PixelsPerScanLine;
        }

        if (fmt == PixelRedGreenBlueReserved8BitPerColor)
        {
                /* Layout: [R][G][B][x] — byte 0=R, 1=G, 2=B */
                for (u32 y = y0; y < y1; ++y)
                {
                        unsigned int *row = base + (size_t)y * stride + x0;
                        for (u32 x = 0; x < span; ++x)
                        {
                                u32 dst = row[x];
                                u8  dr  = (u8)(dst);
                                u8  dg  = (u8)(dst >> 8);
                                u8  db  = (u8)(dst >> 16);
                                u8  or_ = BLEND(src_r, dr, src_a);
                                u8  og  = BLEND(src_g, dg, src_a);
                                u8  ob  = BLEND(src_b, db, src_a);
                                row[x]  = (u32)or_ |
                                          ((u32)og << 8) |
                                          ((u32)ob << 16);
                        }
                }
        }
        else
        {
                /* Layout: [B][G][R][x] — byte 0=B, 1=G, 2=R */
                for (u32 y = y0; y < y1; ++y)
                {
                        unsigned int *row = base + (size_t)y * stride + x0;
                        for (u32 x = 0; x < span; ++x)
                        {
                                u32 dst = row[x];
                                u8  db  = (u8)(dst);
                                u8  dg  = (u8)(dst >> 8);
                                u8  dr  = (u8)(dst >> 16);
                                u8  ob  = BLEND(src_b, db, src_a);
                                u8  og  = BLEND(src_g, dg, src_a);
                                u8  or_ = BLEND(src_r, dr, src_a);
                                row[x]  = (u32)ob |
                                          ((u32)og << 8) |
                                          ((u32)or_ << 16);
                        }
                }
        }

        s_frame_dirty = 1;
}

#undef BLEND

/* -------------------------------------------------------------------------
 * Public accessors
 * ------------------------------------------------------------------------- */
void fill_row_pixels(unsigned int *dst, unsigned int color, size_t n)
{
        fill_row(dst, color, n);
}

unsigned int *get_backbuffer_ptr(void)  { return g_backbuffer; }
unsigned int  get_backbuf_width(void)   { return g_backbuf_width; }
unsigned int  get_backbuf_height(void)  { return g_backbuf_height; }
void          mark_frame_dirty(void)    { s_frame_dirty = 1; }
int           is_frame_dirty(void)      { return s_frame_dirty; }
void          clear_frame_dirty(void)   { s_frame_dirty = 0; }