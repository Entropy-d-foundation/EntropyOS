// SPDX-License-Identifier: GPL-3.0
#ifndef DEBUG_SERIAL_H
#define DEBUG_SERIAL_H

#include <stdarg.h>

/* Low-level serial primitives (COM1) implemented inline so callers
 * don't depend on an external linked serial symbol. */
static inline void outb(unsigned short port, unsigned char val) { __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline unsigned char inb(unsigned short port) { unsigned char ret; __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port)); return ret; }
static inline void serial_print(const char *s) { while (*s) { while (!(inb(0x3FD) & 0x20)); outb(0x3F8, (unsigned char)*s); s++; } }

/* Header-only minimal formatter and helpers so TUs don't depend on a linked
 * serial_printf symbol (avoids link-time removal issues when optimizing). */
static inline void serial_vformat_impl(char *buf, size_t buflen, const char *fmt, va_list ap)
{
    size_t pos = 0;
    while (*fmt && pos + 1 < buflen) {
        if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
        fmt++;
        if (*fmt == '%') { buf[pos++] = '%'; fmt++; continue; }
        if (*fmt == 's') { const char *s = va_arg(ap, const char*); if (!s) s = "(null)"; while (*s && pos + 1 < buflen) buf[pos++] = *s++; fmt++; continue; }
        if (*fmt == 'c') { char c = (char)va_arg(ap, int); buf[pos++] = c; fmt++; continue; }
        if (*fmt == 'd' || *fmt == 'i') { int v = va_arg(ap, int); char tmp[32]; int tp = 0; if (v == 0) tmp[tp++]='0'; int neg=0; unsigned int uv; if (v<0){neg=1; uv=(unsigned int)(-v);} else uv=(unsigned int)v; while (uv>0){tmp[tp++]='0'+(uv%10); uv/=10;} if (neg) tmp[tp++]='-'; while(tp>0 && pos+1<buflen) buf[pos++]=tmp[--tp]; fmt++; continue; }
        if (*fmt == 'u') { unsigned int v = va_arg(ap, unsigned int); char tmp[32]; int tp=0; if (v==0) tmp[tp++]='0'; while(v>0){tmp[tp++]='0'+(v%10); v/=10;} while(tp>0 && pos+1<buflen) buf[pos++]=tmp[--tp]; fmt++; continue; }
        if (*fmt == 'x') { unsigned int v = va_arg(ap, unsigned int); char tmp[32]; int tp=0; const char hex[]="0123456789abcdef"; if (v==0) tmp[tp++]='0'; while(v>0){tmp[tp++]=hex[v&0xF]; v>>=4;} while(tp>0 && pos+1<buflen) buf[pos++]=tmp[--tp]; fmt++; continue; }
        if (fmt[0]=='l' && fmt[1]=='l' && fmt[2]=='x') { unsigned long long v = va_arg(ap, unsigned long long); char tmp[32]; int tp=0; const char hex[]="0123456789abcdef"; if (v==0) tmp[tp++]='0'; while(v>0){tmp[tp++]=hex[v&0xF]; v>>=4;} while(tp>0 && pos+1<buflen) buf[pos++]=tmp[--tp]; fmt+=3; continue; }
        if (*fmt == 'p') { void *p = va_arg(ap, void*); unsigned long long v = (unsigned long long)(uintptr_t)p; if (pos + 3 < buflen) { buf[pos++] = '0'; buf[pos++] = 'x'; } char tmp[32]; int tp=0; const char hex[]="0123456789abcdef"; if (v==0) tmp[tp++]='0'; while (v>0) { tmp[tp++] = hex[v & 0xF]; v >>= 4; } while (tp>0 && pos+1<buflen) buf[pos++] = tmp[--tp]; fmt++; continue; }
        /* Unknown: emit literally */
        buf[pos++] = '%'; if (*fmt) buf[pos++] = *fmt++;
    }
    buf[pos] = '\0';
}

static inline void serial_vprintf(const char *fmt, va_list ap)
{
    char buf[512];
    serial_vformat_impl(buf, sizeof(buf), fmt, ap);
    serial_print(buf);
}

static inline void serial_printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); serial_vprintf(fmt, ap); va_end(ap);
}

/* Simple logging macros that use serial_printf; enable only for installer builds.
 * Define ENABLE_INSTALLER_LOGS in files that should emit persistent logs (installer).
 */
#ifndef ENABLE_INSTALLER_LOGS
#define LOG_INFO(fmt, ...) do { } while(0)
#define LOG_ERROR(fmt, ...) do { } while(0)
#define LOG_CRITICAL(fmt, ...) do { } while(0)
#define LOG_FATAL(fmt, ...) do { } while(0)
#else
#define LOG_INFO(fmt, ...) serial_printf("INFO: " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) serial_printf("ERROR: " fmt "\n", ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) serial_printf("CRITICAL ERROR: " fmt "\n", ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) serial_printf("FATAL ERROR: " fmt "\n", ##__VA_ARGS__)
#endif

#endif /* DEBUG_SERIAL_H */
