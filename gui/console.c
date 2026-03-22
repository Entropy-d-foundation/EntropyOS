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
#include "../init/main.h"
#include "../kernel/console.h"
#include "fonts.h"
#include "helpers.h"
#include <stdbool.h>

extern EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop;

/* in-memory ring buffer for log history (rendered by the UI loop) */
enum { CONSOLE_MAX_LOG_LINES = 64, CONSOLE_MAX_LOG_LINE_LEN = 160 };
static char s_logs[CONSOLE_MAX_LOG_LINES][CONSOLE_MAX_LOG_LINE_LEN];
static int s_logs_next = 0;
static int s_logs_count = 0;

void text_with_pos(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINTN col, UINTN row,
        UINT8 fg, UINT8 bg, const char *s)
{
    if (!gop || !s) return;
    if (!gop->Mode || !gop->Mode->Info) return;

    u8 fr=0, fg_g=0, fb=0;
    u8 br=0, bg_g=0, bb=0;
    switch (fg) {
        case EFI_BLACK: fr=0x00; fg_g=0x00; fb=0x00; break;
        case EFI_BLUE: fr=0x00; fg_g=0x00; fb=0xAA; break;
        case EFI_GREEN: fr=0x00; fg_g=0xAA; fb=0x00; break;
        case EFI_CYAN: fr=0x00; fg_g=0xAA; fb=0xAA; break;
        case EFI_RED: fr=0xAA; fg_g=0x00; fb=0x00; break;
        case EFI_MAGENTA: fr=0xAA; fg_g=0x00; fb=0xAA; break;
        case EFI_BROWN: fr=0xAA; fg_g=0x55; fb=0x00; break;
        case EFI_LIGHTGRAY: fr=0xCC; fg_g=0xCC; fb=0xCC; break;
        case EFI_WHITE: fr=0xFF; fg_g=0xFF; fb=0xFF; break;
        default: fr=0xFF; fg_g=0xFF; fb=0xFF; break;
    }
    switch (bg) {
        case EFI_BLACK: br=0x00; bg_g=0x00; bb=0x00; break;
        case EFI_BLUE: br=0x00; bg_g=0x00; bb=0xAA; break;
        case EFI_GREEN: br=0x00; bg_g=0xAA; bb=0x00; break;
        case EFI_CYAN: br=0x00; bg_g=0xAA; bb=0xAA; break;
        case EFI_RED: br=0xAA; bg_g=0x00; bb=0x00; break;
        case EFI_MAGENTA: br=0xAA; bg_g=0x00; bb=0xAA; break;
        case EFI_BROWN: br=0xAA; bg_g=0x55; bb=0x00; break;
        case EFI_LIGHTGRAY: br=0xCC; bg_g=0xCC; bb=0xCC; break;
        case EFI_WHITE: br=0xFF; bg_g=0xFF; bb=0xFF; break;
        default: br=0x00; bg_g=0x00; bb=0x00; break;
    }

    u32 fg_px = pack_pixel(gop->Mode->Info->PixelFormat, fr, fg_g, fb);
    u32 bg_px = pack_pixel(gop->Mode->Info->PixelFormat, br, bg_g, bb);

    u32 screen_w = gop->Mode->Info->HorizontalResolution;
    u32 screen_h = gop->Mode->Info->VerticalResolution;

    const UINTN char_w = FONT8X16_WIDTH;
    const UINTN char_h = FONT8X16_HEIGHT;
    const UINTN glyph_spacing = 2;

    u32 start_px = (u32)col;
    u32 start_py = (u32)row;
    u32 px = start_px;
    u32 py = start_py;

    for (size_t si = 0; s[si]; ++si) {
        char c = s[si];
        if (c == '\n') {
            px = start_px;
            py += (u32)char_h;
            if (py + char_h > screen_h) break;
            continue;
        }

        const u8 *glyph = font8x16_get((u8)c);
        if (px >= screen_w) {
            px = start_px;
            py += (u32)char_h;
            if (py + char_h > screen_h) break;
        }
        if (py + char_h > screen_h) break;

        /* Fast background fill: compute clipped width once and fill
         * the row either into the backbuffer (if active) or framebuffer */
        unsigned int *back = get_backbuffer_ptr();
        unsigned int back_w = get_backbuf_width();
        unsigned int back_h = get_backbuf_height();
        bool have_back = (back && back_w == screen_w && back_h == screen_h);
        unsigned int pitch = gop->Mode->Info->PixelsPerScanLine;
        unsigned int *fb = (unsigned int*)(uintptr_t)gop->Mode->FrameBufferBase;
        u32 clipped_w = 0;
        if (px < screen_w) {
            u32 max_w = char_w + glyph_spacing;
            if (px + max_w > screen_w) clipped_w = screen_w - px;
            else clipped_w = max_w;
        }

        for (UINTN ry = 0; ry < char_h; ++ry) {
            u32 y = py + (u32)ry;
            if (y >= screen_h) break;
            if (clipped_w == 0) continue;
            if (have_back) {
                fill_row_pixels(back + (size_t)y * back_w + px, bg_px, (size_t)clipped_w);
            } else {
                fill_row_pixels(fb + (size_t)y * pitch + px, bg_px, (size_t)clipped_w);
            }
        }

        if (glyph) {
            for (UINTN gy = 0; gy < FONT8X16_HEIGHT; ++gy) {
                u8 bits = glyph[gy];
                u32 y = py + (u32)gy;
                if (y >= screen_h) break;
                if (px >= screen_w) break;
                if (have_back) {
                    unsigned int *row = back + (size_t)y * back_w + px;
                    for (UINTN gx = 0; gx < FONT8X16_WIDTH && (px + gx) < screen_w; ++gx) {
                        if (bits & (0x80u >> gx)) row[gx] = fg_px;
                    }
                } else {
                    unsigned int *row = fb + (size_t)y * pitch + px;
                    for (UINTN gx = 0; gx < FONT8X16_WIDTH && (px + gx) < screen_w; ++gx) {
                        if (bits & (0x80u >> gx)) row[gx] = fg_px;
                    }
                }
            }
        }

        px += (u32)(char_w + glyph_spacing);
    }
}

void text(const char *s)
{
    if (!s) return;

    /* copy into file-scope ring buffer (truncate if needed) */
    int i = 0;
    while (i < CONSOLE_MAX_LOG_LINE_LEN - 1 && s[i]) { s_logs[s_logs_next][i] = s[i]; ++i; }
    s_logs[s_logs_next][i] = '\0';

    s_logs_next = (s_logs_next + 1) % CONSOLE_MAX_LOG_LINES;
    if (s_logs_count < CONSOLE_MAX_LOG_LINES) s_logs_count++;

    /* Do not draw immediately here; the UI loop (graphics_init.c)
     * will call `console_render_history()` to render the buffered
     * log lines. This prevents duplicate-per-frame appends and
     * centralizes rendering in the graphics loop. */
}

void console_render_history(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    if (!gop) return;

    const UINTN line_h = FONT8X16_HEIGHT + 2;
    UINTN y = 40; /* start rendering logs below the top UI header */

    /* render oldest->newest */
    int start = (s_logs_next - s_logs_count + CONSOLE_MAX_LOG_LINES) % CONSOLE_MAX_LOG_LINES;
    for (int i = 0; i < s_logs_count; ++i)
    {
        int idx = (start + i) % CONSOLE_MAX_LOG_LINES;
        text_with_pos(gop, 8, y + (UINTN)i * line_h, EFI_WHITE, EFI_BLACK, s_logs[idx]);
    }
}
