/* ESP-IDF entry for tests/mcu/mcu_test.c and dec_test.c (vectors embedded with EMBED_FILES). */
#include "esp_cpu.h"
#include "mcu_test.h"

extern const uint8_t pcm_start[] asm("_binary_noisy_pcm_start");
extern const uint8_t pcm_end[] asm("_binary_noisy_pcm_end");
#ifdef EXP_fixed
extern const uint8_t exp_start[] asm("_binary_noisy_fixed_expected_ambe_start");
extern const uint8_t exp_end[] asm("_binary_noisy_fixed_expected_ambe_end");
extern const uint8_t bg_start[] asm("_binary_noisy_fixed_background_expected_ambe_start");
#else
extern const uint8_t exp_start[] asm("_binary_noisy_expected_ambe_start");
extern const uint8_t bg_start[] asm("_binary_noisy_background_expected_ambe_start");
#endif

extern const uint8_t c33_start[] asm("_binary_chip3600_bin_start");
extern const uint8_t c33_end[] asm("_binary_chip3600_bin_end");

static uint32_t ticks(void) { return esp_cpu_get_cycle_count(); }

void app_main(void) {
    ambe_mcu_test(pcm_start, (long)(pcm_end - pcm_start), exp_start, bg_start, ticks);
#ifdef EXP_fixed   /* the decoder's rate-34 input is the fixed-point encoder's expected output */
    ambe_mcu_dec_test(exp_start, (long)(exp_end - exp_start), c33_start, (long)(c33_end - c33_start), ticks);
#endif
}
