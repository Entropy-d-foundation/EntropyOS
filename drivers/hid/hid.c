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

#include "hid.h"
#include "../usb/xhci.h"
#include <stdint.h>
#include "../../drivers/ps2/touchpad.h"
#include "../../init/main.h"
#include "../../kernel/console.h"

/*
 * Minimal USB HID subsystem skeleton.
 * At this stage we only detect whether an xHCI controller exists and return
 * not-implemented. This provides the scaffolding for integrating a full USB
 * HID driver later while letting the cursor code call a single API.
 */

static int g_hid_enabled = 0;

int usb_hid_init(void)
{
    if (xhci_is_present())
    {
        /* debug text removed */
        /* Attempt to initialize PS/2 touchpad as a fallback path so the
         * existing PS/2 polling code can be used even when the input
         * backend prefers USB. This helps VMs where PS/2 is present but
         * the USB host stack is not yet implemented. Ignore failures. */
        (void)ps2_touchpad_init();
        g_hid_enabled = 1;
        return 0;
    }
    /* debug text removed */
    return -1;
}

int usb_hid_poll(int8_t *dx, int8_t *dy, uint8_t *buttons)
{
    if (!g_hid_enabled)
    {
        return 0;
    }

    /* Try forwarding to PS/2 poll implementation first. If PS/2 isn't
     * present or returns no data, synthesize small cursor motion so the
     * UI can be exercised on PCIe-only VMs. This is a temporary test shim. */
    int have = touchpad_poll(dx, dy, buttons);
    if (have)
        return have;

    /* No PS/2 data available and USB HID is not implemented yet. Return
     * 0 to indicate 'no input' rather than synthesizing movement. This
     * prevents the guest cursor from drifting and allows proper host-guest
     * pointer integration when a real USB HID implementation is added. */
    return 0;
}
