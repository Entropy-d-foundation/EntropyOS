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
#include "keyboard.h"
#include <stdint.h>

#define KBD_DATA_PORT  0x60
#define KBD_STATUS_PORT 0x64

#define PS2_STATUS_OUTPUT_BUFFER 0x01

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

int ps2_keyboard_init(void)
{
    /* No special init required for basic polling */
    return 0;
}

int ps2_keyboard_poll(uint8_t *scancode)
{
    if (!scancode) return 0;

    while (inb(KBD_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER) {
        uint8_t b = inb(KBD_DATA_PORT);

        if (b == 0xE0) continue; /* extended, skip for now */
        if ((b & 0x80) || b == 0xFA) continue; /* break codes or ACK */

        *scancode = b & 0x7F;
        return 1;
    }
    return 0;
}
