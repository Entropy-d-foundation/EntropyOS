/* SPDX-License-Identifier: GPL-3.0 */
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
