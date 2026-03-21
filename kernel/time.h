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
#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include <stdint.h>

uint64_t rdtsc(void);
void wait(uint64_t ms);

/* Calibrate TSC frequency using UEFI BootServices->Stall (microseconds).
 * Call this before ExitBootServices so BootServices is available.
 */
void calibrate_tsc(void *system_table_ptr);
uint64_t get_tsc_frequency(void);

#endif // KERNEL_TIME_H