// C-callable pieces for mcu_test.c: printf to the USB serial port, the DWT cycle counter.
#include <Arduino.h>
#include <stdarg.h>

extern "C" int mcu_printf(const char *f, ...) {
    char b[256];
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(b, sizeof(b), f, ap);
    va_end(ap);
    for (char *p = b; *p; p++) {
        if (*p == '\n') Serial.write('\r');
        Serial.write(*p);
    }
    return n;
}

extern "C" uint32_t mcu_ticks(void) { return DWT->CYCCNT; }
