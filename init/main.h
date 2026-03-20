
#ifndef KERNEL_H
#define KERNEL_H

#include "../include/efi.h"

void kernel_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

/* Simple GUI text helper (single-arg) */
void text(const char *s);

#endif // KERNEL_H
