/* SPDX-License-Identifier: GPL-2.0 */
#include "button.h"
#include "helpers.h"
#include "icons.h"
#include "../init/main.h"

/* Internal globals for the simplified button API */
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop = NULL;
static int g_cursor_x = 0;
static int g_cursor_y = 0;
static u8 g_buttons = 0;
static Button g_btn = {0};

void
button_init(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
        g_gop = gop;
        /* default button position/size */
        g_btn.x = 40;
        g_btn.y = 40;
        g_btn.w = ICON_SIZE + 8;
        g_btn.h = ICON_SIZE + 8;
        g_btn.pressed = 0;
        g_btn.icon = NULL;
        g_btn.cb = NULL;
}

void
button_update_cursor(int cursor_x, int cursor_y, u8 buttons)
{
        g_cursor_x = cursor_x;
        g_cursor_y = cursor_y;
        g_buttons = buttons;
}

int
drawButton(const Icon *icon, button_cb_t cb, int x, int y)
{
        if (g_gop == NULL)
        {
                return 0;
        }

        /* prepare button state */
        g_btn.icon = icon;
        g_btn.cb = cb;
        /* set position from caller */
        g_btn.x = x;
        g_btn.y = y;

        /* draw background */
        u32 bg = pack_pixel(g_gop->Mode->Info->PixelFormat, 0xDD, 0xDD, 0xDD);
        u32 border = pack_pixel(g_gop->Mode->Info->PixelFormat, 0x88, 0x88, 0x88);
        draw_box(g_gop, g_btn.x, g_btn.y, g_btn.w, g_btn.h, bg);
        draw_box(g_gop, g_btn.x, g_btn.y, 1, g_btn.h, border);
        draw_box(g_gop, g_btn.x + g_btn.w - 1, g_btn.y, 1, g_btn.h, border);
        draw_box(g_gop, g_btn.x, g_btn.y, g_btn.w, 1, border);
        draw_box(g_gop, g_btn.x, g_btn.y + g_btn.h - 1, g_btn.w, 1, border);

        /* draw icon centered */
        int ix = g_btn.x + (g_btn.w - ICON_SIZE) / 2;
        int iy = g_btn.y + (g_btn.h - ICON_SIZE) / 2;
        draw_icon(g_gop, g_btn.icon, ix, iy, EFI_BLACK);

        /* hit test */
        int inside = (g_cursor_x >= g_btn.x && g_cursor_x < g_btn.x + g_btn.w && g_cursor_y >= g_btn.y && g_cursor_y < g_btn.y + g_btn.h);
        int left = (g_buttons & 1) ? 1 : 0;

        if (inside && left && !g_btn.pressed)
        {
                g_btn.pressed = 1;
                if (g_btn.cb)
                {
                        g_btn.cb();
                }
        }
        if (!left)
        {
                g_btn.pressed = 0;
        }

        return inside;
}

int
drawButtonBox(button_cb_t cb,
              int x, int y, int w, int h, u32 rgba)
{
    if (g_gop == NULL)
        return 0;

    /* prepare button state */
    g_btn.cb   = cb;
    g_btn.x    = x;
    g_btn.y    = y;
    g_btn.w    = w;
    g_btn.h    = h;
    g_btn.rgba = rgba;

    /* --- draw the button --- */
    draw_box(g_gop, g_btn.x, g_btn.y, g_btn.w, g_btn.h, rgba);  // background

    /* optional border */
    u32 border = pack_pixel(g_gop->Mode->Info->PixelFormat, 0x88, 0x88, 0x88);
    draw_box(g_gop, g_btn.x, g_btn.y, 1, g_btn.h, border);           // left
    draw_box(g_gop, g_btn.x + g_btn.w - 1, g_btn.y, 1, g_btn.h, border); // right
    draw_box(g_gop, g_btn.x, g_btn.y, g_btn.w, 1, border);           // top
    draw_box(g_gop, g_btn.x, g_btn.y + g_btn.h - 1, g_btn.w, 1, border); // bottom

    /* --- hit test --- */
    int inside =
        (g_cursor_x >= g_btn.x &&
         g_cursor_x <  g_btn.x + g_btn.w &&
         g_cursor_y >= g_btn.y &&
         g_cursor_y <  g_btn.y + g_btn.h);

    int left = (g_buttons & 1) ? 1 : 0;

    if (inside && left && !g_btn.pressed)
    {
        g_btn.pressed = 1;
        if (cb)
            cb();
    }

    if (!left)
        g_btn.pressed = 0;

    return inside;
}
