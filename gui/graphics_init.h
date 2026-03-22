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
#ifndef KERNEL_GRAPHICS_INIT_H
#define KERNEL_GRAPHICS_INIT_H

#include "../include/efi.h"

/*
 * Initialize graphics subsystem and run the graphics loop
 */
/* Global Graphics Output Protocol pointer set by `kernel_main`. */
extern EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop;

/* Initialize graphics subsystem and run the graphics loop */
void graphics_init_and_run(void);

#endif /* KERNEL_GRAPHICS_INIT_H */
