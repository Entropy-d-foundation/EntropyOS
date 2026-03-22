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
#include "ps2/touchpad.h"
#include "../init/main.h"
#include "../include/debug_serial.h"
#include <stdint.h>

/* I/O ports for PS/2 controller */
#define KBD_DATA_PORT  0x60
#define KBD_STATUS_PORT 0x64
#define KBD_CMD_PORT   0x64

/* Status bits */
#define PS2_STATUS_OUTPUT_BUFFER 0x01
#define PS2_STATUS_INPUT_BUFFER  0x02

/* Helper I/O */
static inline void outb(uint16_t port, uint8_t val)
{
        __asm__ volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

static inline uint8_t inb(uint16_t port)
{
        uint8_t ret;
        __asm__ volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
        return ret;
}

/* Wait for input buffer to be clear */
static void ps2_wait_input()
{
        while (inb(KBD_STATUS_PORT) & PS2_STATUS_INPUT_BUFFER) ;
}

/* Wait for output buffer to have data */
static int ps2_wait_output(uint32_t timeout)
{
        while (timeout--)
        {
                if (inb(KBD_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER)
                {
                        return 1;
                }
        }
        return 0;
}

/* Internal packet assembly */
static uint8_t packet[3];
static int packet_idx = 0;

/* Absolute position tracking for cursor alignment (like Haiku).
 * Tracks cursor position to provide consistent absolute coordinates
 * even though PS/2 only provides relative deltas.
 * Clamped to [0, 32767] to match VirtIO normalize range. */
static int abs_pos_x = 16384;  /* Start at center */
static int abs_pos_y = 16384;

int touchpad_init(void)
{
        /* Enable auxiliary device (PS/2 mouse) */
        ps2_wait_input();
        outb(KBD_CMD_PORT, 0xA8); /* enable aux */

        /* Enable interrupts and auxiliary device in controller config */
        ps2_wait_input();
        outb(KBD_CMD_PORT, 0x20); /* read command byte */
        if (!ps2_wait_output(100000)) return -1;
        uint8_t cfg = inb(KBD_DATA_PORT);

        cfg |= 0x02; /* enable IRQ12 */
        cfg &= ~0x20; /* clear disable mouse */

        ps2_wait_input();
        outb(KBD_CMD_PORT, 0x60); /* write command byte */
        ps2_wait_input();
        outb(KBD_DATA_PORT, cfg);

        /* Tell mouse to use default settings */
        ps2_wait_input();
        outb(KBD_CMD_PORT, 0xD4);
        ps2_wait_input();
        outb(KBD_DATA_PORT, 0xF6); /* set defaults */
        /* Read ACK */
        ps2_wait_output(100000);
        (void)inb(KBD_DATA_PORT);

        /* Enable data reporting */
        ps2_wait_input();
        outb(KBD_CMD_PORT, 0xD4);
        ps2_wait_input();
        outb(KBD_DATA_PORT, 0xF4);
        ps2_wait_output(100000);
        (void)inb(KBD_DATA_PORT);

        packet_idx = 0;
        return 0;
}

int touchpad_poll(int8_t *dx, int8_t *dy, uint8_t *buttons)
{
        int any = 0;
        int total_dx = 0;
        int total_dy = 0;
        uint8_t last_buttons = 0;
        int packet_count = 0;

        /* Read as many bytes/packets as are available to reduce latency */
        while (inb(KBD_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER)
        {
                uint8_t b = inb(KBD_DATA_PORT);
                serial_printf("RAW_BYTE[%d]: 0x%02x\r\n", packet_idx, b);

                /* The first byte of a packet should have bit 3 set in PS/2 protocol */
                if (packet_idx == 0 && !(b & 0x08))
                {
                        /* skip until sync */
                        serial_printf("  -> skipped (no bit 3)\r\n");
                        continue;
                }

                packet[packet_idx++] = b;
                if (packet_idx < 3)
                        continue;

                /* We have a full packet */
                packet_idx = 0;
                packet_count++;

                uint8_t b0 = packet[0];
                uint8_t b1 = packet[1];
                uint8_t b2 = packet[2];

                /* buttons */
                last_buttons = b0 & 0x07;

                /* PS/2 protocol: sign extend X and Y from b0's sign bits (4 and 5).
                   b0[4] = XSg (X sign bit), b0[5] = YSg (Y sign bit).
                   These combine with b1 and b2 to form 9-bit signed values.
                   To sign-extend: if sign bit is set, OR with 0xFFFFFF00 to fill upper bits. */
                int sx = (int)b1;
                if (b0 & 0x10)  /* XSg bit set? */
                    sx |= 0xFFFFFF00;  /* sign extend to negative */
                
                int sy = (int)b2;
                if (b0 & 0x20)  /* YSg bit set? */
                    sy |= 0xFFFFFF00;  /* sign extend to negative */

                serial_printf("PACKET[%d]: b0=0x%02x b1=0x%02x b2=0x%02x -> sx=%d sy=%d btn=0x%x\r\n", 
                              packet_count, b0, b1, b2, sx, sy, last_buttons);

                total_dx += sx;
                total_dy += sy;
                any = 1;
        }

        if (!any)
                return 0;

        /* return accumulated deltas (clamp to int8 range) */
        if (total_dx > 127) total_dx = 127;
        if (total_dx < -128) total_dx = -128;
        if (total_dy > 127) total_dy = 127;
        if (total_dy < -128) total_dy = -128;

        /* Update absolute position for cursor alignment like Haiku.
         * Scale: 2 PS/2 pixels = 1 absolute position unit (range 0-32767) */
        abs_pos_x += (total_dx * 512);  /* ~2px per unit movement */
        abs_pos_y -= (total_dy * 512);  /* -Y because PS/2 Y is inverted */
        
        /* Clamp to normalized range [0, 32767] */
        if (abs_pos_x < 0) abs_pos_x = 0;
        if (abs_pos_x > 32767) abs_pos_x = 32767;
        if (abs_pos_y < 0) abs_pos_y = 0;
        if (abs_pos_y > 32767) abs_pos_y = 32767;

        *dx = (int8_t)total_dx;
        *dy = (int8_t)total_dy;
        *buttons = last_buttons;
        return 1;
}

/* Poll with absolute coordinates (like Haiku VirtIO input device).
 * Returns 1 if event available; abs_x and abs_y are 0.0-1.0 normalized.
 */
int touchpad_poll_absolute(float *abs_x, float *abs_y, uint8_t *buttons,
                           int screen_width, int screen_height)
{
        int8_t dx = 0, dy = 0;
        uint8_t btn = 0;
        
        /* First, perform normal relative polling to update absolute position */
        int have = touchpad_poll(&dx, &dy, &btn);
        
        if (!have)
                return 0;

        /* Convert absolute position to normalized 0.0-1.0 range like Haiku */
        float norm_x = (float)abs_pos_x / 32767.0f;
        float norm_y = (float)abs_pos_y / 32767.0f;

        /* If screen dimensions provided, convert to pixel coordinates */
        if (screen_width > 0 && screen_height > 0) {
                *abs_x = norm_x * (float)screen_width;
                *abs_y = norm_y * (float)screen_height;
        } else {
                /* Return normalized coordinates */
                *abs_x = norm_x;
                *abs_y = norm_y;
        }
        
        *buttons = btn;
        return 1;
}
