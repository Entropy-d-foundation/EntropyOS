/* SPDX-License-Identifier: GPL-3.0 */
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