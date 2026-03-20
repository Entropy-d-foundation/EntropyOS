/* SPDX-License-Identifier: GPL-2.0 */
#include "../../drivers/ps2/touchpad.h"
#include "../../drivers/ps2/keyboard.h"
#include "../../init/main.h"
#include "../../kernel/console.h"
#include "../../include/debug_serial.h"

#include <stdint.h>

/* I/O ports for PS/2 controller */
#define KBD_DATA_PORT  0x60
#define KBD_STATUS_PORT 0x64
#define KBD_CMD_PORT   0x64

/* Status bits */
#define PS2_STATUS_OUTPUT_BUFFER 0x01
#define PS2_STATUS_INPUT_BUFFER  0x02



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
static int abs_pos_x = 16384;
static int abs_pos_y = 16384;



/* Simple PS/2 demultiplexer buffers to avoid keyboard/mouse stealing each other's
 * data. We read hardware bytes into these small ring buffers and let the
 * touchpad and keyboard polls consume from their respective queues. */
#define PS2_BUF_SIZE 64
static uint8_t mouse_buf[PS2_BUF_SIZE];
static uint8_t mouse_head = 0, mouse_tail = 0;
static uint8_t kbd_buf[PS2_BUF_SIZE];
static uint8_t kbd_head = 0, kbd_tail = 0;

/* Debug flag: set to 1 to print parsed mouse packets for diagnostics. */
#define PS2_DEBUG 1

/* Push a byte into a ring buffer, drop if full */
static void ring_push(uint8_t *buf, uint8_t *head, uint8_t tail, uint8_t val)
{
        uint8_t next = (*head + 1) % PS2_BUF_SIZE;
        if (next == tail) {
                /* full */
                return;
        }
        buf[*head] = val;
        *head = next;
}

/* Pop a byte from ring buffer, return -1 if empty, else 0..255 */
static int ring_pop(uint8_t *buf, uint8_t head, uint8_t *tail, uint8_t *out)
{
        if (*tail == head) return -1; /* empty */
        *out = buf[*tail];
        *tail = (*tail + 1) % PS2_BUF_SIZE;
        return 0;
}

/* Drain incoming hardware bytes into mouse/keyboard buffers.
 * Use the PS/2 controller status AUX flag (bit 5) to determine whether the
 * byte originated from the auxiliary (mouse) device or the keyboard. This is
 * reliable and avoids misclassification of keyboard scancodes as mouse data.
 */
#define PS2_STATUS_AUX 0x20
static void ps2_process_incoming()
{
        while (inb(KBD_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER)
        {
                uint8_t status = inb(KBD_STATUS_PORT);
                if (!(status & PS2_STATUS_OUTPUT_BUFFER))
                        break;
                uint8_t b = inb(KBD_DATA_PORT);

                if (status & PS2_STATUS_AUX) {
                        // From auxiliary device (mouse) 
                        ring_push(mouse_buf, &mouse_head, mouse_tail, b);
                        ps2_inc_mouse_byte();
                        //serial_printf("PS2 RAW AUX byte: 0x%02x\r\n", b);
#if PS2_DEBUG
                        //LOG_INFO("PS2: INCOMING status=0x%02x b=0x%02x -> MOUSE", status, b);
#endif
                } else {
                        // From keyboard 
                        ring_push(kbd_buf, &kbd_head, kbd_tail, b);
                        ps2_inc_kbd_byte();
#if PS2_DEBUG
                        //LOG_INFO("PS2: INCOMING status=0x%02x b=0x%02x -> KBD", status, b);
#endif
                }
        }
}

int ps2_touchpad_init(void)
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
        /* debug text removed */
        return 0;
}

/* Read available mouse data from the demuxed mouse buffer and assemble
 * packets. Return 1 and fill dx/dy/buttons when a full packet is ready.
 * Returns 0 if no full packet is available. */
int touchpad_poll(int8_t *dx, int8_t *dy, uint8_t *buttons)
{
        int any = 0;
        int total_dx = 0;
        int total_dy = 0;
        uint8_t last_buttons = 0;

        /* First, move incoming hardware bytes into internal queues */
        ps2_process_incoming();

        /* Try to consume as many full 3-byte mouse packets as available */
        while (1) {
                /* Need at least 3 bytes to form mouse packet */
                uint8_t b0, b1, b2;
                if (ring_pop(mouse_buf, mouse_head, &mouse_tail, &b0) != 0)
                        break; /* not enough */
                if (ring_pop(mouse_buf, mouse_head, &mouse_tail, &b1) != 0) {
                        /* put b0 back and break */
                        mouse_tail = (mouse_tail + PS2_BUF_SIZE - 1) % PS2_BUF_SIZE;
                        break;
                }
                if (ring_pop(mouse_buf, mouse_head, &mouse_tail, &b2) != 0) {
                        /* put b0 and b1 back */
                        mouse_tail = (mouse_tail + PS2_BUF_SIZE - 2) % PS2_BUF_SIZE;
                        break;
                }

                /* The first byte should have bit 3 set; if not, skip to next */
                if (!(b0 & 0x08)) {
                        continue;
                }

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

                /* always print packet regardless of PS2_DEBUG to debug behavior */
                ///////////////////////serial_printf("PS2 PKT b0=0x%02x b1=0x%02x b2=0x%02x sx=%d sy=%d\r\n", b0, b1, b2, sx, sy);

#if PS2_DEBUG
                //////////////LOG_INFO("PS2: MOUSE pkt b0=0x%02x b1=0x%02x b2=0x%02x -> dx=%d dy=%d", b0, b1, b2, sx, sy);
#endif

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

        /* Update absolute position for cursor alignment like Haiku. */
        abs_pos_x += (total_dx * 512);  /* ~2px per unit movement */
        abs_pos_y -= (total_dy * 512);  /* -Y because PS/2 Y is inverted */

        /* Clamp absolute to normalized range [0,32767] */
        if (abs_pos_x < 0) abs_pos_x = 0;
        if (abs_pos_x > 32767) abs_pos_x = 32767;
        if (abs_pos_y < 0) abs_pos_y = 0;
        if (abs_pos_y > 32767) abs_pos_y = 32767;

        *dx = (int8_t)total_dx;
        *dy = (int8_t)total_dy;
        *buttons = last_buttons;
        return 1;
}

/* Minimal PS/2 keyboard support (make code detection). This is intentionally
 * small: it detects key make codes and exposes them to higher layers for
 * logging. It ignores extended sequences and break codes for simplicity.
 */
int ps2_keyboard_init(void)
{
        /* Most PS/2 controllers are ready after the basic controller setup
         * performed elsewhere. We don't need to send special commands here for
         * a basic poll-only implementation. Return success. */
        return 0;
}

int ps2_keyboard_poll(uint8_t *scancode)
{
        /* Drain incoming bytes into our demux queues first */
        ps2_process_incoming();

        /* Pop bytes from the keyboard queue, ignoring extended prefix and
         * break codes and ACKs. */
        uint8_t b;
        while (ring_pop(kbd_buf, kbd_head, &kbd_tail, &b) == 0)
        {
                if (b == 0xE0) {
                        /* Extended sequence - skip for now */
                        continue;
                }

                /* Ignore break codes (set 1: MSB set) and ACKs (0xFA) */
                if ((b & 0x80) || b == 0xFA) {
                        continue;
                }

                /* We have a make code */
                *scancode = b & 0x7F;
                return 1;
        }
        return 0;
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


