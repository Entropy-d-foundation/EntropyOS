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
#ifndef GUI_ICONS_H
#define GUI_ICONS_H

#include "../include/efi.h"
#include <stdint.h>

#define ICON_SIZE 32

/*
 * Structure to represent an icon
 */
typedef struct
{
        /*
         * Icon data where non-zero values represent pixels to draw
         */
        char pixels[ICON_SIZE][ICON_SIZE];
} Icon;

/*
 * Function to draw an icon at specified coordinates with EFI colors
 */
void draw_icon(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, const Icon *icon, uint32_t x, uint32_t y, UINT8 color);

/*
 * Loading icon
 */
extern const Icon CURSOR;
extern const Icon LOGO;
extern const Icon SHUTDOWN;
extern const Icon REBOOT;
extern const Icon ENTER;

#endif