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
#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

// 8x16 fixed font
#define FONT8X16_WIDTH 8
#define FONT8X16_HEIGHT 16

// Returns a pointer to a 16-byte glyph for ASCII character c (32..127).
// Each byte is one scanline (8 pixels). MSB is leftmost pixel.
// Returns NULL if glyph is not available.
const uint8_t *font8x16_get(uint8_t c);

// Register a glyph from a textual 16-line representation where
// '#' = pixel on, '.' = pixel off. Lines shorter than 8 are right-padded with '.'.
// Returns 0 on success, -1 on error.
int font8x16_register_from_text(uint8_t c, const char *lines[FONT8X16_HEIGHT]);
// fonts.h (add prototype)
void fonts_init(void);

#endif // FONTS_H
