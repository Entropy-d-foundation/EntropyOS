/* SPDX-License-Identifier: GPL-2.0 */
#include "debug_serial.h"
#include "../include/kernel/main.h"
#include <stdarg.h>
#include <stddef.h>

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

static inline u8 inb(u16 port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

void serial_print(const char *s)
{
    /* Wait for serial ready and send bytes to COM1 (0x3F8) */
    while (*s)
    {
        while (!(inb(0x3FD) & 0x20)) ;
        outb(0x3F8, (u8)*s);
        s++;
    }
}

/* Minimal formatting engine supporting %s, %d, %u, %x, %llx, %p, %c and %% */
static void serial_vformat(char *buf, size_t buflen, const char *fmt, va_list ap)
{
    size_t pos = 0;
    while (*fmt && pos + 1 < buflen) {
        if (*fmt != '%') {
            buf[pos++] = *fmt++;
            continue;
        }
        fmt++; /* skip % */
        if (*fmt == '%') { buf[pos++] = '%'; fmt++; continue; }

        /* Simple handlers */
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && pos + 1 < buflen) buf[pos++] = *s++;
            fmt++; continue;
        }
        if (*fmt == 'c') {
            char c = (char)va_arg(ap, int);
            buf[pos++] = c; fmt++; continue;
        }
        if (*fmt == 'd' || *fmt == 'i') {
            int v = va_arg(ap, int);
            char tmp[32]; int tp = 0;
            if (v == 0) tmp[tp++] = '0';
            int neg = 0;
            unsigned int uv;
            if (v < 0) { neg = 1; uv = (unsigned int)(-v); } else { uv = (unsigned int)v; }
            while (uv > 0) { tmp[tp++] = '0' + (uv % 10); uv /= 10; }
            if (neg) tmp[tp++] = '-';
            while (tp > 0 && pos + 1 < buflen) buf[pos++] = tmp[--tp];
            fmt++; continue;
        }
        if (*fmt == 'u') {
            unsigned int v = va_arg(ap, unsigned int);
            char tmp[32]; int tp = 0;
            if (v == 0) tmp[tp++] = '0';
            while (v > 0) { tmp[tp++] = '0' + (v % 10); v /= 10; }
            while (tp > 0 && pos + 1 < buflen) buf[pos++] = tmp[--tp];
            fmt++; continue;
        }
        if (*fmt == 'x') {
            unsigned int v = va_arg(ap, unsigned int);
            char tmp[32]; int tp = 0;
            const char hex[] = "0123456789abcdef";
            if (v == 0) tmp[tp++] = '0';
            while (v > 0) { tmp[tp++] = hex[v & 0xF]; v >>= 4; }
            while (tp > 0 && pos + 1 < buflen) buf[pos++] = tmp[--tp];
            fmt++; continue;
        }
        if (fmt[0] == 'l' && fmt[1] == 'l' && fmt[2] == 'x') {
            unsigned long long v = va_arg(ap, unsigned long long);
            char tmp[32]; int tp = 0;
            const char hex[] = "0123456789abcdef";
            if (v == 0) tmp[tp++] = '0';
            while (v > 0) { tmp[tp++] = hex[v & 0xF]; v >>= 4; }
            while (tp > 0 && pos + 1 < buflen) buf[pos++] = tmp[--tp];
            fmt += 3; continue;
        }
        if (*fmt == 'p') {
            void *p = va_arg(ap, void*);
            unsigned long long v = (unsigned long long)(uintptr_t)p;
            /* print as 0x... */
            if (pos + 3 < buflen) {
                buf[pos++] = '0'; buf[pos++] = 'x';
            }
            char tmp[32]; int tp = 0; const char hex[] = "0123456789abcdef";
            if (v == 0) tmp[tp++] = '0';
            while (v > 0) { tmp[tp++] = hex[v & 0xF]; v >>= 4; }
            while (tp > 0 && pos + 1 < buflen) buf[pos++] = tmp[--tp];
            fmt++; continue;
        }
        /* Unknown format: emit as-is */
        buf[pos++] = '%';
        if (*fmt) buf[pos++] = *fmt++;
    }
    buf[pos] = '\0';
}

