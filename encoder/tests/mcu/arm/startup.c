/* Bare-metal Cortex-M start-up for QEMU's MPS2 boards: vector table, FPU enable, SysTick tick counter, and
 * the test (printf through semihosting, newlib rdimon). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mcu_test.h"

extern const unsigned char noisy_pcm[], noisy_expect[], noisy_expect_bg[];
extern const unsigned int noisy_pcm_len;
extern uint32_t __stack_top, __etext, __data_start__, __data_end__, __bss_start__, __bss_end__;
extern void initialise_monitor_handles(void);
int main(void);

#define SYST_CSR (*(volatile uint32_t *)0xE000E010)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018)
#define CPACR (*(volatile uint32_t *)0xE000ED88)

static void reset(void) {
    uint32_t *s = &__etext, *d;
    for (d = &__data_start__; d < &__data_end__;) *d++ = *s++;
    for (d = &__bss_start__; d < &__bss_end__;) *d++ = 0;
#if defined(__ARM_FP)
    CPACR |= 0xF << 20;   /* CP10, CP11 full access */
    __asm__ volatile("dsb\n isb");
#endif
    initialise_monitor_handles();   /* newlib rdimon: stdio through semihosting */
    exit(main());
}

static void hang(void) { for (;;) {} }

__attribute__((section(".vectors"), used)) static void (*const vectors[16])(void) = {
    (void (*)(void))&__stack_top, reset, hang, hang, hang, hang, hang, 0, 0, 0, 0, hang, hang, 0, hang, hang};

/* SysTick on the processor clock, extended to 32 bits (call at least every 2^24 ticks) */
static uint32_t ticks(void) {
    static uint32_t last = 0xFFFFFF, acc;
    uint32_t now = SYST_CVR;
    acc += (last - now) & 0xFFFFFF;
    last = now;
    return acc;
}

int main(void) {
    SYST_RVR = 0xFFFFFF;
    SYST_CVR = 0;
    SYST_CSR = 5;
    ambe_mcu_test(noisy_pcm, (long)noisy_pcm_len, noisy_expect, noisy_expect_bg, ticks);
    return 0;
}

void _init(void) {}
void _fini(void) {}
