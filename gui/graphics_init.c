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
#include "../drivers/poweroffreboot.h"
#include "graphics_init.h"
#include "helpers.h"
#include "../kernel/console.h"
#include "../kernel/time.h"
#include "../kernel/utils.h"
#include "fonts.h"
#include "icons.h"
#include "button.h"
#include "../drivers/ps2/touchpad.h"
#include "../drivers/hid/keyboard.h"
#include "../init/main.h"
#include "../scheduler/scheduler.h"
#include <stdint.h>
#include <stdbool.h>
#include "../include/debug_serial.h"

/* Scheduler-driven boxes: one task per box */
#define SCHED_BOX_COUNT 5
static double sched_box_y[SCHED_BOX_COUNT];
static int sched_box_x[SCHED_BOX_COUNT];
static unsigned int sched_box_color[SCHED_BOX_COUNT];

#define SCHED_BOX_SPEED 160.0 /* pixels per second - adjustable */

static void box_move_task(void *arg, double dt)
{
    int idx = (int)(intptr_t)arg;
    if (idx < 0 || idx >= SCHED_BOX_COUNT) return;
    if (!g_gop || !g_gop->Mode || !g_gop->Mode->Info) return;

    int screen_h = g_gop->Mode->Info->VerticalResolution;

    /* Move based on elapsed time for smooth, framerate-independent motion */
    sched_box_y[idx] += SCHED_BOX_SPEED * dt;
    if (sched_box_y[idx] > (double)screen_h) sched_box_y[idx] = 0.0;
}

/* Variables that lets the user customize stuff in the GUI*/
bool g_light_theme = false;

static int g_button_toggled = 0;

/* Right-click placed RGBA box state */
static int g_left_box_active = 0;
static int g_left_box_x = 0;
static int g_left_box_y = 0;
static u32  g_left_box_rgba = 0xFF000088; /* default semi-opaque red */

/* Small UI caches to avoid re-rendering expensive translucent areas every frame.
 * - `taskbar_cache` stores a single row-strip at the bottom (max height = 64).
 * - `menu_cache` stores the start-menu panel (320x400) since it's drawn often
 *   while the menu is open. These caches are filled when the UI changes and
 *   copied into the backbuffer on subsequent frames.
 *
 * FIX: taskbar cache fast-path now always calls drawButton on top of the blit
 *      so button hit regions are re-registered every frame. Without this,
 *      button_update_cursor finds no registered button after frame 1 and all
 *      clicks are silently ignored. */
#define TASKBAR_CACHE_MAX_W 1920
#define TASKBAR_CACHE_MAX_H 64
static unsigned int taskbar_cache[TASKBAR_CACHE_MAX_W * TASKBAR_CACHE_MAX_H];
static int taskbar_cache_w = 0;
static int taskbar_cache_h = 0;
static int taskbar_cache_theme = -1; /* theme used to build cache */
static int taskbar_cache_valid = 0;

#define MENU_CACHE_W 320
#define MENU_CACHE_H 400
static unsigned int menu_cache[MENU_CACHE_W * MENU_CACHE_H];
static int menu_cache_w = MENU_CACHE_W;
static int menu_cache_h = MENU_CACHE_H;
static int menu_cache_valid = 0;

EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop = NULL;

void draw_round_box(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
              u32 x0, u32 y0,
              u32 w, u32 h,
              u32 r,
              u32 color)
{
        if (!gop || !gop->Mode || !gop->Mode->Info)
                return;

        u32 sx = gop->Mode->Info->HorizontalResolution;
        u32 sy = gop->Mode->Info->VerticalResolution;

        if (x0 >= sx || y0 >= sy)
                return;

        u32 x1 = x0 + w; if (x1 > sx) x1 = sx;
        u32 y1 = y0 + h; if (y1 > sy) y1 = sy;

        for (u32 y = y0; y < y1; y++)
        {
                for (u32 x = x0; x < x1; x++)
                {
                // Top-left corner
                if (x < x0 + r && y < y0 + r)
                {
                        int dx = (x0 + r) - x;
                        int dy = (y0 + r) - y;
                        int rr = (int)r;
                        if (dx*dx + dy*dy > rr*rr) continue;
                }

                                // Top-right corner
                                if (x >= x1 - r && y < y0 + r)
                                {
                                        int dx = x - (x1 - r);
                                        int dy = (y0 + r) - y;
                                        int rr = (int)r;
                                        if (dx*dx + dy*dy > rr*rr) continue;
                                }

                                // Bottom-left corner
                                if (x < x0 + r && y >= y1 - r)
                                {
                                        int dx = (x0 + r) - x;
                                        int dy = y - (y1 - r);
                                        int rr = (int)r;
                                        if (dx*dx + dy*dy > rr*rr) continue;
                                }

                                // Bottom-right corner
                                if (x >= x1 - r && y >= y1 - r)
                                {
                                        int dx = x - (x1 - r);
                                        int dy = y - (y1 - r);
                                        int rr = (int)r;
                                        if (dx*dx + dy*dy > rr*rr) continue;
                                }

                put_pixel(gop, x, y, color);
                }
        }
}

void start_menu()
{
        g_button_toggled = !g_button_toggled;
        /* FIX: invalidate menu cache whenever the menu is toggled so the panel
         * is fully redrawn on next open rather than showing stale pixels. */
        menu_cache_valid = 0;
}

void SecondMenuThing()
{
        if (g_button_toggled)
        {
                g_button_toggled = false;
        }
        else
        {
                return;
        }
}

void login()
{
        // do nothing
}

void graphics_init_and_run(void)
{
        /* Harden: caller must set g_gop before calling us. If it is NULL every
         * subsequent access to g_gop->Mode->Info would triple-fault the kernel. */
        if (!g_gop || !g_gop->Mode || !g_gop->Mode->Info) {
                serial_printf("graphics_init_and_run: g_gop not ready, aborting\r\n");
                return;
        }

        fonts_init();
        init_backbuffer(g_gop);
        button_init(g_gop);

        const double box_vpxs = 40.0;
        bool running = true;

        /* Initialize scheduler boxes and register one task per box */
        {
            int screen_w = (int)g_gop->Mode->Info->HorizontalResolution;
            const int box_w = 50;
            /* space the boxes evenly across the screen instead of hard-coded X offsets */
            for (int i = 0; i < SCHED_BOX_COUNT; ++i) {
                int spacing = screen_w / (SCHED_BOX_COUNT + 1);
                sched_box_x[i] = spacing * (i + 1) - box_w / 2;
                sched_box_y[i] = (double)(i * -40);
                sched_box_color[i] = pack_pixel(g_gop->Mode->Info->PixelFormat,
                                               (i+1)*30 % 256, 255 - ((i*40) % 256), (i*70) % 256);
                scheduler_add_task(box_move_task, (void*)(intptr_t)i);
            }
        }

        /* Cursor position persists across frames. Initialize at screen center.
         * Relative PS/2 motion is applied directly to these coordinates. */
        static int cursor_x = 0;
        static int cursor_y = 0;
        static int cursor_initialized = 0;

        u64 tsc_freq = get_tsc_frequency();
        const int TARGET_FPS = 1000;
        const u64 TARGET_TICKS = tsc_freq / TARGET_FPS;
        u64 last_tsc = rdtsc();

        /* frame-time samples for a simple FPS indicator (circular buffer) */
        enum { FPS_SAMPLES = 64 };
        uint64_t fps_samples[FPS_SAMPLES] = {0};
        int fps_idx = 0;
        int fps_count = 0;
        uint64_t fps_sum = 0;

        /* exponential moving averages (profile counters, in TSC ticks) */
        uint64_t icon_avg = 0, taskbar_avg = 0, menu_avg = 0, sched_avg = 0, console_avg = 0;
        const int EMA_SHIFT = 3; /* avg = (avg*(2^EMA_SHIFT-1) + new) / 2^EMA_SHIFT */

        while (running)
        {
                uint64_t frame_start = rdtsc();
                uint64_t delta_ticks = frame_start - last_tsc;
                double delta_s = (double)delta_ticks / (double)tsc_freq;
                last_tsc = frame_start;

                /* cache screen dims for this frame and update FPS samples */
                int screen_w = (int)g_gop->Mode->Info->HorizontalResolution;
                int screen_h = (int)g_gop->Mode->Info->VerticalResolution;

                /* Initialize cursor to screen center on first frame */
                if (!cursor_initialized) {
                        cursor_x = screen_w / 2;
                        cursor_y = screen_h / 2;
                        cursor_initialized = 1;
                }

                fps_sum -= fps_samples[fps_idx];
                fps_samples[fps_idx] = delta_ticks;
                fps_sum += fps_samples[fps_idx];
                fps_idx = (fps_idx + 1) % FPS_SAMPLES;
                if (fps_count < FPS_SAMPLES) fps_count++;

                clear_screen_fb(g_gop, 0x06, 0x27, 0x44);

                /* profile console rendering */
                uint64_t _t_console_s = rdtsc();
                console_render_history(g_gop);
                uint64_t _t_console_e = rdtsc();
                uint64_t _console_dt = (_t_console_e - _t_console_s);
                console_avg = ((console_avg * ((1ULL << EMA_SHIFT) - 1)) + _console_dt) >> EMA_SHIFT;

                /* Run all scheduled tasks once per frame for smooth updates */
                uint64_t _t_sched_s = rdtsc();
                scheduler_run_all(delta_s);
                uint64_t _t_sched_e = rdtsc();
                uint64_t _sched_dt = (_t_sched_e - _t_sched_s);
                sched_avg = ((sched_avg * ((1ULL << EMA_SHIFT) - 1)) + _sched_dt) >> EMA_SHIFT;

                /* Draw all scheduler boxes (measure draw cost) */
                uint64_t _t_drawboxes_s = rdtsc();
                for (int i = 0; i < SCHED_BOX_COUNT; ++i) {
                    /* Wrap is handled in box_move_task; no duplicate check here */
                    draw_box(g_gop, sched_box_x[i], (u32)sched_box_y[i], 50, 50, sched_box_color[i]);
                }
                uint64_t _t_drawboxes_e = rdtsc();
                uint64_t _drawboxes_dt = (_t_drawboxes_e - _t_drawboxes_s);
                /* include drawboxes in scheduler avg to keep counters short */
                sched_avg = ((sched_avg * ((1ULL << EMA_SHIFT) - 1)) + _drawboxes_dt) >> EMA_SHIFT;

                /* Poll input early so UI buttons see the current button state
                 * and clicks register in the same frame */
                s8 poll_dx = 0, poll_dy = 0;
                u8 poll_buttons = 0;
                int poll_have;
                static int absolute_mode = 0;

                if (!absolute_mode) {
                    poll_have = usb_touchpad_poll(&poll_dx, &poll_dy, &poll_buttons);
                    if (poll_have) {
                        //serial_printf("GUI INPUT: dx=%d dy=%d btn=0x%x\r\n", poll_dx, poll_dy, poll_buttons);
                        int raw_dx = poll_dx;
                        int raw_dy = poll_dy;
                        if (poll_dx > 32)  poll_dx =  32;
                        if (poll_dx < -32) poll_dx = -32;
                        if (poll_dy > 32)  poll_dy =  32;
                        if (poll_dy < -32) poll_dy = -32;
                        {
                            static int sat_count = 0;
                            static int saw_x_sat = 0, saw_y_sat = 0;
                            if (raw_dx == 127 || raw_dx == -128) saw_x_sat = 1;
                            if (raw_dy == 127 || raw_dy == -128) saw_y_sat = 1;
                            if (raw_dx == 127 || raw_dx == -128 ||
                                raw_dy == 127 || raw_dy == -128) {
                                sat_count++;
                            } else {
                                sat_count = 0;
                                saw_x_sat = saw_y_sat = 0;
                            }
                            if (sat_count >= 3 && saw_x_sat && saw_y_sat) {
                                absolute_mode = 1;
                                serial_printf("GUI: switching to absolute input mode\r\n");
                            }
                        }
                        //serial_printf("GUI CLAMPED: raw=(%d,%d) -> clamped=(%d,%d)\r\n",
                        //              raw_dx, raw_dy, poll_dx, poll_dy);
                    }
                } else {
                    float absx = 0.0f, absy = 0.0f;
                    poll_have = touchpad_poll_absolute(&absx, &absy, &poll_buttons,
                                                       screen_w, screen_h);
                    if (poll_have) {
                        cursor_x = (int)absx;
                        cursor_y = (int)absy;
                        /* Harden: clamp immediately — driver could return negative or
                         * out-of-range coords on glitch/miscalibration. */
                        if (cursor_x < 0) cursor_x = 0;
                        if (cursor_x >= screen_w) cursor_x = screen_w - 1;
                        if (cursor_y < 0) cursor_y = 0;
                        if (cursor_y >= screen_h) cursor_y = screen_h - 1;
                        //serial_printf("GUI ABS: x=%d y=%d btn=0x%x\r\n", cursor_x, cursor_y, poll_buttons);
                    }
                }

                /* Apply relative motion directly to cursor position */
                if (poll_have && !absolute_mode) {
                    cursor_x += poll_dx;
                    cursor_y -= poll_dy; /* PS/2 Y is inverted */

                    if (cursor_x < 0) cursor_x = 0;
                    if (cursor_x >= screen_w) cursor_x = screen_w - 1;
                    if (cursor_y < 0) cursor_y = 0;
                    if (cursor_y >= screen_h) cursor_y = screen_h - 1;

                    //if (poll_dx != 0 || poll_dy != 0)
                        //serial_printf("CURPOS: x=%d y=%d\r\n", cursor_x, cursor_y);
                }

                /* ---------------------------------------------------------------
                 * Taskbar
                 * FIX: compute button geometry once and use it for BOTH the draw
                 *      path and the close-menu hit-test below so coords never
                 *      diverge. drawButton is called unconditionally every frame
                 *      (on top of the cache blit when the cache is valid) so that
                 *      button hit regions are re-registered with the button system
                 *      on every frame. Without this, button_update_cursor finds no
                 *      registered button after frame 1 and all clicks are ignored.
                 * --------------------------------------------------------------- */
                int btn_w = ICON_SIZE + 8;
                int btn_h = ICON_SIZE + 8;
                int taskbar_h = btn_h;
                int taskbar_y = screen_h - taskbar_h;

                /* canonical start-button position — single definition used everywhere */
                int start_btn_x = 8;
                int start_btn_y = taskbar_y + (taskbar_h - btn_h) / 2;

                {
                    u32 taskbar_rgba = g_light_theme ? 0x808080CC : 0x202020CC;

                    uint64_t _t_taskbar_s = rdtsc();
                    unsigned int *back = get_backbuffer_ptr();
                    unsigned int back_w = get_backbuf_width();

                    if (back && taskbar_cache_valid && taskbar_cache_w == screen_w &&
                        taskbar_cache_h == taskbar_h &&
                        taskbar_cache_theme == (g_light_theme ? 1 : 0))
                    {
                        /* Fast path: blit cached pixels */
                        for (int yy = 0; yy < taskbar_h; ++yy) {
                            unsigned int *dst = back + (size_t)(taskbar_y + yy) * back_w;
                            unsigned int *src = taskbar_cache + (size_t)yy * taskbar_cache_w;
                            for (int xx = 0; xx < taskbar_cache_w; ++xx) dst[xx] = src[xx];
                        }
                        mark_frame_dirty();

                        /* FIX: always call drawButton so hit region is re-registered.
                         * drawButton overdrawing the same pixels is harmless and cheap. */
                        drawButton(&LOGO, start_menu, start_btn_x, start_btn_y);
                    }
                    else
                    {
                        /* Slow path: full draw, then capture into cache */
                        draw_box_rgba(g_gop, 0, taskbar_y, (u32)screen_w,
                                      (u32)taskbar_h, taskbar_rgba);
                        drawButton(&LOGO, start_menu, start_btn_x, start_btn_y);

                        if (back && back_w == (unsigned)screen_w &&
                            get_backbuf_height() == (unsigned)screen_h)
                        {
                            for (int yy = 0; yy < taskbar_h; ++yy) {
                                unsigned int *src = back + (size_t)(taskbar_y + yy) * back_w;
                                unsigned int *dst = taskbar_cache + (size_t)yy * screen_w;
                                for (int xx = 0; xx < screen_w; ++xx) dst[xx] = src[xx];
                            }
                            taskbar_cache_w     = screen_w;
                            taskbar_cache_h     = taskbar_h;
                            taskbar_cache_theme = (g_light_theme ? 1 : 0);
                            taskbar_cache_valid = 1;
                        }
                    }

                    uint64_t _t_taskbar_e = rdtsc();
                    uint64_t _taskbar_dt  = (_t_taskbar_e - _t_taskbar_s);
                    taskbar_avg = ((taskbar_avg * ((1ULL << EMA_SHIFT) - 1)) + _taskbar_dt) >> EMA_SHIFT;
                }

                /* Start menu */
                if (g_button_toggled)
                {
                        const int menu_w   = 320;
                        const int menu_h   = 400;
                        const int divider_h = 2;
                        const int bottom_h  = 49;

                        int menu_x = 8;
                        int menu_y = screen_h - taskbar_h - menu_h;
                        if (menu_y < 8) menu_y = 8;

                        u32 panel_color   = g_light_theme ? 0xE0E0E0CC : 0x202020CC;
                        u32 divider_color = g_light_theme ? 0xC8C8C8CC : 0x101010CC;
                        u32 bottom_color  = g_light_theme ? 0xB4B4B4CC : 0x303030CC;

                        uint64_t _t_menu_s = rdtsc();
                        unsigned int *back = get_backbuffer_ptr();

                        if (back && menu_cache_valid &&
                            menu_cache_w == menu_w && menu_cache_h == menu_h)
                        {
                            /* copy cached menu into backbuffer */
                            unsigned int back_w = get_backbuf_width();
                            for (int yy = 0; yy < menu_h; ++yy) {
                                unsigned int *dst = back + (size_t)(menu_y + yy) * back_w + menu_x;
                                unsigned int *src = menu_cache + (size_t)yy * menu_w;
                                for (int xx = 0; xx < menu_w; ++xx) dst[xx] = src[xx];
                            }
                            mark_frame_dirty();

                            /* Re-register menu buttons on top of cache blit */
                            int menu_btn_w  = ICON_SIZE + 8;
                            int menu_btn_h  = ICON_SIZE + 8;
                            int right_pad   = 8;
                            int spacing     = 5;
                            int divider_y   = menu_y + menu_h - (bottom_h + divider_h);
                            int bottom_y    = divider_y + divider_h;
                            int shutdown_x  = menu_x + menu_w - menu_btn_w - right_pad;
                            int reboot_x    = shutdown_x - (menu_btn_w + spacing);
                            int menu_btn_y  = bottom_y + (bottom_h - menu_btn_h) / 2;
                            drawButton(&SHUTDOWN, shutdown, shutdown_x, menu_btn_y);
                            drawButton(&REBOOT,   reboot,   reboot_x,   menu_btn_y);
                        }
                        else
                        {
                            draw_box_rgba(g_gop, menu_x, menu_y, menu_w, menu_h, panel_color);

                            int divider_y  = menu_y + menu_h - (bottom_h + divider_h);
                            draw_box_rgba(g_gop, menu_x, divider_y, menu_w, divider_h, divider_color);
                            int bottom_y   = divider_y + divider_h;
                            draw_box_rgba(g_gop, menu_x, bottom_y,  menu_w, bottom_h,  bottom_color);

                            int menu_btn_w = ICON_SIZE + 8;
                            int menu_btn_h = ICON_SIZE + 8;
                            int right_pad  = 8;
                            int spacing    = 5;
                            int shutdown_x = menu_x + menu_w - menu_btn_w - right_pad;
                            int reboot_x   = shutdown_x - (menu_btn_w + spacing);
                            int menu_btn_y = bottom_y + (bottom_h - menu_btn_h) / 2;

                            drawButton(&SHUTDOWN, shutdown, shutdown_x, menu_btn_y);
                            drawButton(&REBOOT,   reboot,   reboot_x,   menu_btn_y);

                            if (back &&
                                get_backbuf_width()  == (unsigned)screen_w &&
                                get_backbuf_height() == (unsigned)screen_h)
                            {
                                unsigned int back_w = get_backbuf_width();
                                for (int yy = 0; yy < menu_h; ++yy) {
                                    unsigned int *src = back + (size_t)(menu_y + yy) * back_w + menu_x;
                                    unsigned int *dst = menu_cache + (size_t)yy * menu_w;
                                    for (int xx = 0; xx < menu_w; ++xx) dst[xx] = src[xx];
                                }
                                menu_cache_valid = 1;
                            }
                        }

                        uint64_t _t_menu_e = rdtsc();
                        uint64_t _menu_dt  = (_t_menu_e - _t_menu_s);
                        menu_avg = ((menu_avg * ((1ULL << EMA_SHIFT) - 1)) + _menu_dt) >> EMA_SHIFT;
                }

                /* Draw right-click placed box if active */
                if (g_left_box_active) {
                    draw_box_rgba(g_gop, (u32)g_left_box_x, (u32)g_left_box_y,
                                  ICON_SIZE, ICON_SIZE, g_left_box_rgba);
                }

                /*
                 * Cursor movement, clicks, etc.
                 */
                {
                        u8 buttons = poll_buttons;
                        int have   = poll_have;

                        /* Diagnostics: PS/2 classification counters top-right */
                        int mbytes = 0, kbytes = 0;
                        ps2_get_and_clear_counters(&mbytes, &kbytes);
                        if (mbytes || kbytes) {
                                char buf[64];
                                int pos = 0;
                                buf[pos++] = 'M'; buf[pos++] = ':';
                                if (mbytes == 0) buf[pos++] = '0'; else {
                                        int v = mbytes; char tmp[12]; int tp=0;
                                        if (v<0) { buf[pos++]='-'; v=-v; }
                                        while (v>0) { tmp[tp++]='0'+(v%10); v/=10; }
                                        while (tp>0) buf[pos++]=tmp[--tp];
                                }
                                buf[pos++] = ' ';
                                buf[pos++] = 'K'; buf[pos++] = ':';
                                if (kbytes == 0) buf[pos++] = '0'; else {
                                        int v = kbytes; char tmp[12]; int tp=0;
                                        if (v<0) { buf[pos++]='-'; v=-v; }
                                        while (v>0) { tmp[tp++]='0'+(v%10); v/=10; }
                                        while (tp>0) buf[pos++]=tmp[--tp];
                                }
                                buf[pos]=0;
                                text_with_pos(g_gop, screen_w - 120, 0, 0xFF, 0x00, buf);
                        }

                        /* FPS indicator */
                        if (fps_count > 0) {
                                uint64_t avg_ticks = fps_sum / (uint64_t)fps_count;
                                int fps_i = (int)((double)tsc_freq / (double)avg_ticks + 0.5);
                                char fpsbuf[16]; int p = 0;
                                if (fps_i == 0) fpsbuf[p++] = '0'; else {
                                        int v = fps_i; char tmp[8]; int tp = 0;
                                        while (v > 0) { tmp[tp++] = '0' + (v % 10); v /= 10; }
                                        while (tp > 0) fpsbuf[p++] = tmp[--tp];
                                }
                                fpsbuf[p++] = 'F'; fpsbuf[p++] = 'P'; fpsbuf[p++] = 'S';
                                fpsbuf[p] = 0;
                                text_with_pos(g_gop, screen_w - 64, 0,
                                              EFI_LIGHTGRAY, EFI_BLACK, fpsbuf);

                                char profbuf[64]; int pp = 0;
                                uint64_t icon_us    = (icon_avg    * 1000000ULL) / tsc_freq;
                                uint64_t taskbar_us = (taskbar_avg * 1000000ULL) / tsc_freq;
                                uint64_t menu_us    = (menu_avg    * 1000000ULL) / tsc_freq;
                                uint64_t sched_us   = (sched_avg   * 1000000ULL) / tsc_freq;

                                profbuf[pp++]='I'; profbuf[pp++]=':';
                                if (icon_us==0) profbuf[pp++]='0'; else { uint64_t v=icon_us; char tmp[8]; int tp=0; while(v>0){tmp[tp++]='0'+(v%10);v/=10;} while(tp>0)profbuf[pp++]=tmp[--tp]; }
                                profbuf[pp++]=' ';
                                profbuf[pp++]='T'; profbuf[pp++]=':';
                                if (taskbar_us==0) profbuf[pp++]='0'; else { uint64_t v=taskbar_us; char tmp[8]; int tp=0; while(v>0){tmp[tp++]='0'+(v%10);v/=10;} while(tp>0)profbuf[pp++]=tmp[--tp]; }
                                profbuf[pp++]=' ';
                                profbuf[pp++]='M'; profbuf[pp++]=':';
                                if (menu_us==0) profbuf[pp++]='0'; else { uint64_t v=menu_us; char tmp[8]; int tp=0; while(v>0){tmp[tp++]='0'+(v%10);v/=10;} while(tp>0)profbuf[pp++]=tmp[--tp]; }
                                profbuf[pp++]=' ';
                                profbuf[pp++]='S'; profbuf[pp++]=':';
                                if (sched_us==0) profbuf[pp++]='0'; else { uint64_t v=sched_us; char tmp[8]; int tp=0; while(v>0){tmp[tp++]='0'+(v%10);v/=10;} while(tp>0)profbuf[pp++]=tmp[--tp]; }
                                profbuf[pp]=0;
                                text_with_pos(g_gop, screen_w - 160, 12,
                                              EFI_LIGHTGRAY, EFI_BLACK, profbuf);
                        }

                        /* Any key press hides the cursor */
                        static int cursor_hidden = 0;
                        uint8_t sc = 0;
                        if (keyboard_get_scancode_and_clear(&sc))
                                cursor_hidden = 1;

                        if (cursor_hidden && !have) {
                                buttons = 0;
                        } else {
                                if (cursor_hidden) cursor_hidden = 0;
                                if (cursor_x < 0) cursor_x = 0;
                                if (cursor_y < 0) cursor_y = 0;
                                if (cursor_x > screen_w - ICON_SIZE) cursor_x = screen_w - ICON_SIZE;
                                if (cursor_y > screen_h - ICON_SIZE) cursor_y = screen_h - ICON_SIZE;

                                uint64_t _t_icon_s = rdtsc();
                                draw_icon(g_gop, &CURSOR, cursor_x, cursor_y, EFI_BLACK);
                                uint64_t _t_icon_e = rdtsc();
                                uint64_t _icon_dt  = (_t_icon_e - _t_icon_s);
                                icon_avg = ((icon_avg * ((1ULL << EMA_SHIFT) - 1)) + _icon_dt) >> EMA_SHIFT;

                                button_update_cursor(cursor_x, cursor_y, buttons);

                                {
                                        static uint8_t prev_buttons = 0;
                                        uint8_t new_press = buttons & (uint8_t)(~prev_buttons);

                                        /* Any click outside open menu -> close it.
                                         * FIX: use the same start_btn_x/start_btn_y computed
                                         *      above so the hit-test rect always matches where
                                         *      drawButton actually placed the button. */
                                        if (new_press & 0x3) {
                                                if (g_button_toggled) {
                                                        int menu_x = 8;
                                                        int menu_w = 320;
                                                        int menu_h = 400;
                                                        int menu_y = screen_h - taskbar_h - menu_h;
                                                        if (menu_y < 8) menu_y = 8;

                                                        int inside_menu =
                                                                (cursor_x >= menu_x &&
                                                                 cursor_x <  menu_x + menu_w &&
                                                                 cursor_y >= menu_y &&
                                                                 cursor_y <  menu_y + menu_h);

                                                        /* use canonical start_btn_x/y from above */
                                                        int inside_start_btn =
                                                                (cursor_x >= start_btn_x &&
                                                                 cursor_x <  start_btn_x + btn_w &&
                                                                 cursor_y >= start_btn_y &&
                                                                 cursor_y <  start_btn_y + btn_h);

                                                        if (!(inside_menu || inside_start_btn)) {
                                                                g_button_toggled = 0;
                                                                menu_cache_valid  = 0;
                                                        }
                                                }

                                                /* Click outside right-click box -> remove it */
                                                if (g_left_box_active) {
                                                        if (!(cursor_x >= g_left_box_x &&
                                                              cursor_x <  g_left_box_x + ICON_SIZE &&
                                                              cursor_y >= g_left_box_y &&
                                                              cursor_y <  g_left_box_y + ICON_SIZE)) {
                                                                g_left_box_active = 0;
                                                        }
                                                }
                                        }

                                        /* Right-click box placement */
                                        if (new_press & 0x2) {
                                                if (!g_left_box_active) {
                                                        int dx0 = 0, dy0 = 0;
                                                        for (int yy = 0; yy < ICON_SIZE; ++yy) {
                                                                for (int xx = 0; xx < ICON_SIZE; ++xx) {
                                                                        if (CURSOR.pixels[yy][xx] != '.') {
                                                                                dx0 = xx; dy0 = yy;
                                                                                goto box_found;
                                                                        }
                                                                }
                                                        }
box_found:
                                                        g_left_box_x    = cursor_x + dx0;
                                                        g_left_box_y    = cursor_y + dy0;
                                                        g_left_box_rgba = g_light_theme
                                                                          ? 0x0000FF88
                                                                          : 0xFF000088;
                                                        g_left_box_active = 1;
                                                } else {
                                                        if (cursor_x >= g_left_box_x &&
                                                            cursor_x <  g_left_box_x + ICON_SIZE &&
                                                            cursor_y >= g_left_box_y &&
                                                            cursor_y <  g_left_box_y + ICON_SIZE) {
                                                                g_left_box_active = 0;
                                                        }
                                                }
                                        }

                                        prev_buttons = buttons;
                                }
                        }

                        /* cursor bounds already enforced above before draw_icon */
                }

                /* Rate-limit: measure how long THIS frame actually took so the
                 * sleep duration is accurate. Using delta_ticks (previous frame)
                 * was wrong — under heavy load we'd oversleep, wasting a frame. */
                uint64_t frame_end = rdtsc();
                uint64_t frame_duration = frame_end - frame_start;
                uint64_t remaining =
                        (frame_duration < TARGET_TICKS) ? (TARGET_TICKS - frame_duration) : 0;

                refresh_frame_ticks(remaining, g_gop);
        }
}