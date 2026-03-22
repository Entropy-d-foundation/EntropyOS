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
