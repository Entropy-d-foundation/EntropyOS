#include <stdint.h>

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_putc(char c) {
    /* wait for transmitter holding register empty */
    while (!(inb(0x3FD) & 0x20));
    outb(0x3F8, (unsigned char)c);
}

void serial_write(const char *s) {
    while (*s) serial_putc(*s++);
}

/* Entry point symbol expected by the bootloader */
void kern_entry(void) {
    serial_write("Legacy kernel started via BIOS loader\n");

    /* simple VGA text write at top-left via BIOS text mode buf in memory */
    volatile unsigned short *vga = (volatile unsigned short *)0xB8000;
    const char *msg = "EntropyOS legacy mode kernel\0";
    for (int i = 0; msg[i]; ++i) {
        vga[i] = (unsigned short)(msg[i] | (0x07 << 8));
    }

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
