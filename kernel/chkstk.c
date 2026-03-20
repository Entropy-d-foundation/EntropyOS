/* A universal stub for the Microsoft-style stack check helper.
 *
 * Certain compilers (GCC targeting MinGW/Windows or clang when
 * emitting MSVC-compatible code) will automatically insert a call to
 * `__chkstk_ms` (and rarely `___chkstk_ms`) at the start of functions
 * which allocate large amounts of stack space.  When building a freestanding
 * kernel/UEFI binary we obviously do not provide the actual runtime
 * support, but the mere reference will cause the linker to fail if the
 * symbol is missing.
 *
 * Historically the project contained a dummy definition in
 * `gui/fonts.c`, which is only compiled into the main iso/BOOT.EFI image.
 * Other binaries (installer, bootloader) pulled in dependencies such as
 * `fs/exfat/exfat.c` which could trigger the same reference and thus
 * resulted in unpredictable linker errors.
 *
 * To make the behaviour consistent we provide a single source that is
 * added to every link line and therefore guarantees the symbol is
 * available regardless of which subset of sources are being linked.
 */

/* Canonical name expected by the toolchains. */
void __chkstk_ms(void) { }

/* Some old/strange toolchains or ABI variants may emit three underscores. */
void ___chkstk_ms(void) __attribute__((alias("__chkstk_ms")));
