#ifndef MCU_TEST_H
#define MCU_TEST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void ambe_mcu_test(const uint8_t *pcm, long bytes, const uint8_t *expect, const uint8_t *expect_bg,
                   uint32_t (*read_ticks)(void));
/* decoder: rate-34 frames (7 bytes each) and rate-33 frames (9 bytes each); needs dec_expect.h (dec_hashes.c) */
void ambe_mcu_dec_test(const uint8_t *f34, long bytes34, const uint8_t *f33, long bytes33, uint32_t (*read_ticks)(void));
#ifdef __cplusplus
}
#endif
#endif
