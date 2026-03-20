// EntropyOS/src/boot.c
#include "../../include/efi.h"
#include "../../init/main.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
        EFI_SYSTEM_TABLE *SystemTable)
{
        (void)ImageHandle;
        /* kernel handles locating GOP, drawing, and exiting boot services */
        kernel_main(ImageHandle, SystemTable);

        while (1) {} /* just in case */
        return EFI_SUCCESS;
}
