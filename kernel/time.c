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
#include "time.h"
#include "../include/efi.h"
#include <stdint.h>

static uint64_t s_tsc_freq = 3000000000ULL;

uint64_t rdtsc(void)
{
        unsigned int lo, hi;
        __asm__ __volatile__(
                ".intel_syntax noprefix\n\t"
                "rdtsc\n\t"
                "mov %0, eax\n\t"
                "mov %1, edx\n\t"
                ".att_syntax prefix"
                : "=r"(lo), "=r"(hi)  // output operands
                :                      // no input
                : "eax", "edx"         // clobbered registers
        );
        return ((uint64_t)hi << 32) | lo;
}

void wait(uint64_t ms)
{
        uint64_t start = rdtsc();
        uint64_t freq = s_tsc_freq;
        uint64_t end = start + (freq / 1000) * ms;
        while (rdtsc() < end)
        {
                __asm__ __volatile__("pause");
        }
}

uint64_t get_tsc_frequency(void)
{
    return s_tsc_freq;
}

/* Calibrate the TSC frequency using UEFI BootServices->Stall.
 * `system_table_ptr` should be the `EFI_SYSTEM_TABLE *` passed to
 * `kernel_main` (void* used to avoid header cycles). This function
 * will call the BootServices Stall function to wait a known microsecond
 * count and measure rdtsc delta to compute ticks/second.
 */
void calibrate_tsc(void *system_table_ptr)
{
    if (!system_table_ptr) return;
    EFI_SYSTEM_TABLE *st = (EFI_SYSTEM_TABLE *)system_table_ptr;
    if (!st || !st->BootServices) return;

        /* Use the typed BootServices->Stall member declared in include/efi.h */
        EFI_STALL stall = st->BootServices->Stall;
        if (!stall) return;

        /* measure over 100ms to get a stable reading */
        const UINTN usec = 100000; /* 100000 us = 100 ms */
        uint64_t t0 = rdtsc();
        stall(usec);
        uint64_t t1 = rdtsc();
    if (t1 > t0) {
        uint64_t ticks = t1 - t0;
        /* ticks per second = ticks / 0.1s */
        s_tsc_freq = ticks * 10ULL;
    }
}