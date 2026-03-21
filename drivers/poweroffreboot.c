/*
    EntropyOS
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
#include "poweroffreboot.h"

#ifndef SYSCONTROL_H
#define SYSCONTROL_H

//#include "efi.h"

static inline void do_io_wait(void) { __asm__ __volatile__("outb %%al, $0x80" : : "a"(0)); }

/* Try to reset via the 0xCF9 port (PC reset control). */
static inline void reset_via_cf9(void) {
	/* 0x06 = reset CPU + assert reset; 0x02 = full reset; many systems accept 0x06 */
	__asm__ __volatile__(
		"mov $0x06, %%al\n"
		"mov $0x0cf9, %%dx\n"
		"out %%al, %%dx"
		: : : "al", "dx");
	do_io_wait();
}

/* Try keyboard controller pulse (legacy): 0xFE to port 0x64 (might reboot on some systems) */
static inline void reset_via_kbd(void) {
	__asm__ __volatile__("mov $0xfe, %%al\nout %%al, $0x64" : : : "al");
	do_io_wait();
}

/* Cause triple fault by loading an empty IDT and forcing an interrupt -> CPU resets on many platforms. */
static inline void triple_fault_reset(void) {
	struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr = {0, 0};
	__asm__ __volatile__("lidt %0\nint $3" : : "m"(idtr));
	for (;;) __asm__ __volatile__("hlt");
}

void reboot()
{
	/* Try a sequence of resets known to work on many platforms/emulators */
	reset_via_cf9();
	reset_via_kbd();
	triple_fault_reset();
}

void shutdown()
{
	/* Try several emulator/vendor-specific shutdown ports in order:
	 * - QEMU/OVMF: port 0x604, write 0x2000
	 * - Bochs/QEMU older: port 0xB004, write 0x2000
	 * - isa-debug-exit device: port 0x501, write a byte (0 triggers exit)
	 * If none succeed, halt forever.
	 */
	/* attempt port 0x604 */
	__asm__ __volatile__(
		"mov $0x2000, %%ax\n"
		"mov $0x0604, %%dx\n"
		"out %%ax, %%dx\n"
		: : : "ax", "dx");
	do_io_wait();

	/* attempt port 0xB004 */
	__asm__ __volatile__(
		"mov $0x2000, %%ax\n"
		"mov $0xB004, %%dx\n"
		"out %%ax, %%dx\n"
		: : : "ax", "dx");
	do_io_wait();

	/* attempt isa-debug-exit at port 0x501 (outb) */
	__asm__ __volatile__(
		"mov $0x00, %%al\n"
		"mov $0x0501, %%dx\n"
		"out %%al, %%dx\n"
		: : : "al", "dx");
	do_io_wait();

	for (;;) __asm__ __volatile__("hlt");
}

#endif // SYSCONTROL_H
