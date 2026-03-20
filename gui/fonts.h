// EntropyOS/src/crutches/fonts.h
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
