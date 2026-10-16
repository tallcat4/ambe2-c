/* Encoder test for microcontrollers: encodes the noisy test vector (normal and background-noise mode), checks the
 * frames against the expected ones and reports counter ticks per 20 ms frame. The platform supplies the data, a
 * tick counter (read_ticks) and printf. The calibration loop runs 100 x 1000 nops, so under an instruction-counting
 * emulator the result relates the counter to executed instructions. */
#include <stdio.h>
#include <string.h>

#include "ambe_enc.h"
#include "mcu_test.h"

#ifdef FX_PROFILE
static const char *const STAGE[] = {"fft", "split+log2", "comb", "level+silence", "pitch prefilter", "pitch MLP",
                                    "viterbi", "voicing features", "voicing MLP", "background", "amplitudes",
                                    "quantiser"};
static uint32_t (*prof_ticks)(void), prof_last;
static uint64_t prof_acc[16];
void fx_prof_mark(int stage) {
    uint32_t now = prof_ticks();
    prof_acc[stage] += now - prof_last;
    prof_last = now;
}
#endif

#if defined(AMBE_ENC_FIXED)
#define MEM_BYTES 14000
#elif defined(AMBE_ENC_FLOAT)
#define MEM_BYTES 32000
#else
#define MEM_BYTES 64000
#endif
#ifdef MCU_TEST_MALLOC   /* state on the heap (e.g. ESP32 with the encoder in RAM: static DRAM is short) */
#include <stdlib.h>
static double *mem;
#define MEM_SIZE MEM_BYTES
#else
static double mem[MEM_BYTES / sizeof(double)];   /* >= ambe_enc_size() (checked) */
#define MEM_SIZE sizeof(mem)
#endif

static void run(const char *name, int background, const uint8_t *pcm, long n, const uint8_t *expect,
                uint32_t (*read_ticks)(void)) {
    long k, out = 0, same = 0;
    uint32_t c, cmax = 0;
    uint64_t total = 0;
    uint8_t fr[7];
    int16_t x[160];
    ambe_encoder *e = ambe_enc_init(mem);
    ambe_enc_set_background(e, background);
    for (k = 0; k <= n; k++) {
        int got;
        if (k < n) memcpy(x, pcm + 320 * k, 320);
        c = read_ticks();
#ifdef FX_PROFILE
        prof_ticks = read_ticks;
        prof_last = c;
#endif
        got = k < n ? ambe_enc_push(e, x, fr) : ambe_enc_flush(e, fr);
        c = read_ticks() - c;
        total += c;
        if (c > cmax) cmax = c;
        if (got) same += !memcmp(fr, expect + 7 * out++, 7);
    }
#ifdef FX_PROFILE
    {
        int i;
        for (i = 0; i < 12; i++) {
            printf("  %-17s %8lu ticks/frame\n", STAGE[i], (unsigned long)(prof_acc[i] / (uint64_t)n));
            prof_acc[i] = 0;
        }
    }
#endif
    printf("%s: %ld/%ld frames identical to the expected ones; ticks/frame mean %lu max %lu\n", name, same, out,
           (unsigned long)(total / (uint64_t)n), (unsigned long)cmax);
}

void ambe_mcu_test(const uint8_t *pcm, long bytes, const uint8_t *expect, const uint8_t *expect_bg,
                   uint32_t (*read_ticks)(void)) {
    uint32_t c = read_ticks();
    int i;
    for (i = 0; i < 1000; i++) __asm__ volatile(".rept 100\n nop\n .endr");   /* short enough for any branch range */
    c = read_ticks() - c;
    printf("calibration: %lu ticks for 100000 nops\n", (unsigned long)c);
    printf("encoder state %lu bytes\n", ambe_enc_size());
#ifdef MCU_TEST_MALLOC
    if (!mem) mem = malloc(MEM_BYTES);
#endif
    if (ambe_enc_size() > MEM_SIZE) {
        printf("state buffer too small\n");
        return;
    }
    run("normal mode", 0, pcm, bytes / 320, expect, read_ticks);
    run("background mode", 1, pcm, bytes / 320, expect_bg, read_ticks);
    printf("AMBE_ENC_TEST_DONE\n");
}
