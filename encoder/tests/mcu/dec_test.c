/* Decoder (blip25c) test for microcontrollers: decodes a rate-34 stream (the encoder's noisy test vector output) and
 * a rate-33 stream (FEC, AMBE-3000R output), compares FNV-1a 64 hashes of the PCM with the host's (dec_expect.h,
 * from dec_hashes.c) and reports counter ticks per 20 ms frame. */
#include <stdio.h>

#include "blip25.h"
#include "dec_expect.h"
#include "mcu_test.h"

#ifdef MCU_TEST_MALLOC
#include <stdlib.h>
static blip25_decoder *decp;
#define dec (*decp)
#else
static blip25_decoder dec;
#endif

static uint64_t fnv(uint64_t h, const int16_t *p, int n) {
    int i;
    for (i = 0; i < n; i++) {
        h = (h ^ (uint8_t)(p[i] & 0xff)) * 0x100000001b3ULL;
        h = (h ^ (uint8_t)((p[i] >> 8) & 0xff)) * 0x100000001b3ULL;
    }
    return h;
}

uint64_t ambe_dec_run(const uint8_t *frames, long n, int rate33, uint32_t (*read_ticks)(void), uint64_t *total,
                      uint32_t *cmax) {
    int16_t pcm[160];
    uint64_t h = 0xcbf29ce484222325ULL;
    long k;
    blip25_init(&dec);
    *total = 0, *cmax = 0;
    for (k = 0; k < n; k++) {
        uint32_t c = read_ticks ? read_ticks() : 0;
        if (rate33)
            blip25_decode_3600(&dec, frames + 9 * k, pcm, NULL);
        else
            blip25_decode_2450_thumbdv(&dec, frames + 7 * k, pcm);
        if (read_ticks) {
            c = read_ticks() - c;
            *total += c;
            if (c > *cmax) *cmax = c;
        }
        h = fnv(h, pcm, 160);
    }
    return h;
}

void ambe_mcu_dec_test(const uint8_t *f34, long b34, const uint8_t *f33, long b33, uint32_t (*read_ticks)(void)) {
    uint64_t total, h;
    uint32_t cmax;
    printf("decoder state %lu bytes\n", (unsigned long)sizeof(blip25_decoder));
#ifdef MCU_TEST_MALLOC
    if (!decp) decp = malloc(sizeof(blip25_decoder));
#endif
    h = ambe_dec_run(f34, b34 / 7, 0, read_ticks, &total, &cmax);
    printf("decoder rate 34: %ld frames %s; ticks/frame mean %lu max %lu\n", b34 / 7,
           h == DEC_EXPECT_34 ? "identical to the host" : "DIFFERENT from the host",
           (unsigned long)(total / (uint64_t)(b34 / 7)), (unsigned long)cmax);
    h = ambe_dec_run(f33, b33 / 9, 1, read_ticks, &total, &cmax);
    printf("decoder rate 33: %ld frames %s; ticks/frame mean %lu max %lu\n", b33 / 9,
           h == DEC_EXPECT_33 ? "identical to the host" : "DIFFERENT from the host",
           (unsigned long)(total / (uint64_t)(b33 / 9)), (unsigned long)cmax);
    printf("AMBE_DEC_TEST_DONE\n");
}
