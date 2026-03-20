/* SPDX-License-Identifier: GPL-2.0 */
#include "../../drivers/ps2/touchpad.h"

/* Internal counters live in this file so they're always emitted and
 * accessible. Provide increment helpers for the PS/2 driver to call and an
 * accessor for the GUI to read/clear them. */
static volatile int g_ps2_mouse_bytes = 0;
static volatile int g_ps2_kbd_bytes = 0;

void ps2_inc_mouse_byte(void) { g_ps2_mouse_bytes++; }
void ps2_inc_kbd_byte(void) { g_ps2_kbd_bytes++; }

void ps2_get_and_clear_counters(int *mouse_bytes, int *kbd_bytes)
{
    if (mouse_bytes) { *mouse_bytes = g_ps2_mouse_bytes; g_ps2_mouse_bytes = 0; }
    if (kbd_bytes) { *kbd_bytes = g_ps2_kbd_bytes; g_ps2_kbd_bytes = 0; }
}
