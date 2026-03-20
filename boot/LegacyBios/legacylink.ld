/* Linker script to place the legacy kernel at 0x00100000 (1MiB)
   Kernel will be loaded there before entry into long mode. */
ENTRY(kern_entry)
SECTIONS
{
  . = 0x00100000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss  : { *(.bss*) }
}
