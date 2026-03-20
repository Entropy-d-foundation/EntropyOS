// SPDX-License-Identifier: GPL-2.0
#include "utils.h"
#include "../gui/helpers.h"
#include "../kernel/console.h"
#include "time.h"

void refresh(uint64_t ms, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
        /* Blit first only if the backbuffer changed; otherwise just wait to
         * pace the frame rate — skipping the blit reduces CPU and memory
         * bandwidth on static screens. */
        if (is_frame_dirty()) blit_backbuffer(gop);
        wait(ms);
}

void refresh_frame_ticks(uint64_t target_ticks, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
        /* Hybrid frame pacing: do a coarse millisecond wait for longer
         * gaps and then a short busy-wait for the final few milliseconds.
         * This reduces CPU spin time while preserving low jitter for the
         * final frame alignment. */
        if (!gop) return;

        /* Only blit if the backbuffer is dirty. */
        if (is_frame_dirty()) blit_backbuffer(gop);

        uint64_t freq = get_tsc_frequency();
        /* if there's more than 10ms remaining, sleep coarsely first */
        const uint64_t TEN_MS = freq / 100; /* 10 ms */
        if (target_ticks > TEN_MS) {
                uint64_t coarse_ticks = target_ticks - TEN_MS;
                uint64_t ms = coarse_ticks / (freq / 1000);
                if (ms > 0) wait(ms);
                /* short spin for the final ~10ms */
                uint64_t now = rdtsc();
                uint64_t end = now + TEN_MS;
                while (rdtsc() < end) { __asm__ __volatile__("pause"); }
                return;
        }

        uint64_t start = rdtsc();
        uint64_t end = start + target_ticks;
        while (rdtsc() < end) {
                __asm__ __volatile__("pause");
        }
}

void big_fault(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, const char *msg)
{
        (void)msg;
        clear_screen_fb(gop, 0x00, 0x00, 0x00);
        /* debug text removed */
        refresh(16, gop);
        __asm__ __volatile__("hlt");
}