/* Bare-metal RV32 start-up for QEMU's "virt" board: stack, .data / .bss, a minimal printf (%s %d %u %ld %lu, width,
 * '-') to the 16550 UART, the retired-instruction counter as the tick source, exit through the SiFive test device. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include "mcu_test.h"

extern const unsigned char noisy_pcm[], noisy_expect[], noisy_expect_bg[];
extern const unsigned int noisy_pcm_len;
extern uint32_t __bss_start__, __bss_end__;
int main(void);

__attribute__((naked, section(".text.start"))) void _start(void) {
    __asm__ volatile(".option push\n .option norelax\n la gp, __global_pointer$\n .option pop\n la sp, __stack_top\n j reset_c");
}

/* the C library of the toolchain may target a particular SoC (ESP-IDF's does): own memcpy / memset / memmove */
#define NO_LIBCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))   /* keep the loops loops */
NO_LIBCALL void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
NO_LIBCALL void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b)
        while (n--) *a++ = *b++;
    else
        while (n--) a[n] = b[n];
    return d;
}
NO_LIBCALL void *memset(void *d, int c, size_t n) {
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}

__attribute__((used)) void reset_c(void) {   /* .data is loaded in place (RAM image): only .bss is cleared */
    volatile uint32_t *d;
    for (d = &__bss_start__; d < &__bss_end__;) *d++ = 0;
    main();
    *(volatile uint32_t *)0x100000 = 0x5555;   /* SiFive test: power off */
    for (;;) {}
}

static void putch(char c) {
    while (!(*(volatile uint8_t *)0x10000005 & 0x20)) {}
    *(volatile uint8_t *)0x10000000 = (uint8_t)c;
}

int printf(const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    for (; *f; f++) {
        char buf[24], *p;
        int width = 0, left = 0, len, isneg = 0, lng = 0;
        unsigned long u = 0;
        if (*f != '%') {
            putch(*f);
            continue;
        }
        f++;
        if (*f == '-') left = 1, f++;
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == 'l') lng = 1, f++;
        if (*f == 's') {
            p = va_arg(ap, char *);
        } else {
            if (*f == 'd') {
                long v = lng ? va_arg(ap, long) : va_arg(ap, int);
                isneg = v < 0;
                u = isneg ? -(unsigned long)v : (unsigned long)v;
            } else {
                u = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            }
            p = buf + sizeof(buf) - 1;
            *p = 0;
            do *--p = (char)('0' + u % 10); while (u /= 10);
            if (isneg) *--p = '-';
        }
        for (len = 0; p[len]; len++) {}
        if (!left)
            for (; width > len; width--) putch(' ');
        while (*p) putch(*p++);
        for (; width > len; width--) putch(' ');
    }
    va_end(ap);
    return 0;
}

static uint32_t ticks(void) {   /* retired instructions (machine-mode counter) */
    uint32_t v;
    __asm__ volatile("csrr %0, minstret" : "=r"(v));
    return v;
}

int main(void) {
    ambe_mcu_test(noisy_pcm, (long)noisy_pcm_len, noisy_expect, noisy_expect_bg, ticks);
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
NO_LIBCALL int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}
int puts(const char *s) {
    while (*s) putch(*s++);
    putch('\n');
    return 0;
}
