// SPDX-License-Identifier: GPL-3.0
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