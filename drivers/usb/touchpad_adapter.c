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
#include "../../init/main.h"
#include "../hid/hid.h"
#include "../usb/xhci.h"
#include "../../drivers/ps2/touchpad.h"

static int g_inited = 0;
static int g_using_usb = 0;

int usb_touchpad_init(void)
{
    if (g_inited) return 0;

    /* Prefer USB if a controller is present */
    if (xhci_is_present())
    {
        if (usb_hid_init() == 0)
        {
            g_using_usb = 1;
            g_inited = 1;
            return 0;
        }
        /* fall through and try PS/2 */
    }

    /* Try PS/2 touchpad as a fallback */
    if (ps2_touchpad_init() == 0)
    {
        g_using_usb = 0;
        g_inited = 1;
        return 0;
    }

    /* Nothing available */
    g_inited = 1; /* mark attempted */
    return -1;
}

int usb_touchpad_poll(int8_t *dx, int8_t *dy, uint8_t *buttons)
{
    if (!g_inited)
        return 0;

    /* If using USB path, try that first and fall back to PS/2 if no data */
    if (g_using_usb)
    {
        int have = usb_hid_poll(dx, dy, buttons);
        if (have) return have;

        /* try PS/2 fallback if present */
        have = touchpad_poll(dx, dy, buttons);
        if (have) return have;

        /* No input available: return 0 so higher layers can treat this as 'no input'. */
        return 0;
    }

    /* Using PS/2 path */
    return touchpad_poll(dx, dy, buttons);
}
