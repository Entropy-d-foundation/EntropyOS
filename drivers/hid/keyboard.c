/*
    GloamOS
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
#include "keyboard.h"
#include "../ps2/keyboard.h"
#include "../ps2/touchpad.h" /* for ps2 presence if needed */
#include <stddef.h>
#include "../../include/debug_serial.h"
#include <stdint.h>

/* Store last scancode (0 == none) */
static volatile uint8_t g_last_scancode = 0;

int keyboard_init(void)
{
    /* For now prefer PS/2 keyboard (USB keyboard not implemented). Initialize
     * PS/2 as a fallback and always return success for the polling-based
     * implementation used by the scheduler task. */
    (void)ps2_keyboard_init();
    LOG_INFO("KBD: initialized (PS/2 fallback)");
    return 0;
}

void keyboard_task(void *arg, double dt)
{
    (void)arg; (void)dt;
    uint8_t sc;
    static int poll_ctr = 0;

    if (ps2_keyboard_poll(&sc)) {
#if 1
        LOG_INFO("KBD: scancode=0x%02x", sc);
#endif
        /* Store the last make code; GUI pulls it and clears it. */
        g_last_scancode = sc;
        /* reset poll counter so we log status less when activity is present */
        poll_ctr = 0;
        return;
    }

    /* Periodically log the PS/2 controller status to detect whether any
     * output is ever reported. This is rate-limited to avoid serial spam. */
    if (++poll_ctr % 200 == 0) {
        uint8_t status;
        __asm__ volatile ("inb %1, %0" : "=a" (status) : "Nd" ((uint16_t)0x64));
        LOG_INFO("KBD: ps2 status=0x%02x", status);
    }
}

int keyboard_get_scancode_and_clear(uint8_t *scancode)
{
    if (!scancode) return 0;
    uint8_t v = g_last_scancode;
    g_last_scancode = 0;
    if (v == 0) return 0;
    *scancode = v;
    return 1;
}
