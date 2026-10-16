// Encoder and decoder test on the Arduino Portenta H7 (Cortex-M7 core): tests/mcu/mcu_test.c and dec_test.c with
// the DWT cycle counter,
// output on the USB serial port. Built by run_portenta.sh (which adds the encoder sources and test vectors).
#include "mcu_test.h"

extern "C" const unsigned char noisy_pcm[], noisy_expect[], noisy_expect_bg[];
extern "C" const unsigned int noisy_pcm_len, dec_r34_len, dec_r33_len;
extern "C" const unsigned char dec_r34[], dec_r33[];
extern "C" int mcu_printf(const char *f, ...);
extern "C" uint32_t mcu_ticks(void);

void setup() {
    Serial.begin(115200);
    while (!Serial) {}
    delay(500);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55;   // unlock (Cortex-M7)
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    mcu_printf("core clock %lu Hz, I-cache %s, D-cache %s\n", (unsigned long)SystemCoreClock,
               (SCB->CCR & SCB_CCR_IC_Msk) ? "on" : "off", (SCB->CCR & SCB_CCR_DC_Msk) ? "on" : "off");
    ambe_mcu_test(noisy_pcm, (long)noisy_pcm_len, noisy_expect, noisy_expect_bg, mcu_ticks);
    ambe_mcu_dec_test(dec_r34, (long)dec_r34_len, dec_r33, (long)dec_r33_len, mcu_ticks);
}

void loop() {}
