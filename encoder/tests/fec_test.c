/* fec_test [chip3600.bin] : checks ambe_enc_to_rate33 / ambe_enc_bits_to_rate33 (src/fec.c).
 *  1. frames measured on the AMBE-3000R: rate 34 -> rate 33 as the chip does it (docs/encoder.md), and every
 *     error-free frame of the chip's rate-33 vector re-encodes to itself;
 *  2. the code itself: the weight distributions of c0 (extended Golay [24,12]) and of c1 without the PN sequence
 *     (Golay [23,12]) are the theoretical ones (minimum distances 8 and 7);
 *  3. error correction through the decoder (blip25_fec_3600, hard and soft): every pattern of 1..3 errors in c0 or
 *     c1 is corrected with the right count, every 4-error pattern in c0 is detected, errors in c2 / c3 pass through.
 * Positions in the 72-bit frame come from mbelib's rW / rX / rY / rZ tables (as in pyambelib), independently of
 * the Annex S table in src/fec.c. Links the decoder library (../decoder). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ambe_enc.h"
#include "blip25.h"

static const int rW[36] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
                           0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2};
static const int rX[36] = {23, 10, 22, 9, 21, 8, 20, 7, 19, 6, 18, 5, 17, 4, 16, 3, 15, 2,
                           14, 1, 13, 0, 12, 10, 11, 9, 10, 8, 9, 7, 8, 6, 7, 5, 6, 4};
static const int rY[36] = {0, 2, 0, 2, 0, 2, 0, 2, 0, 3, 0, 3, 1, 3, 1, 3, 1, 3,
                           1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3};
static const int rZ[36] = {5, 3, 4, 2, 3, 1, 2, 0, 1, 13, 0, 12, 22, 11, 21, 10, 20, 9,
                           19, 8, 18, 7, 17, 6, 16, 5, 15, 4, 14, 3, 13, 2, 12, 1, 11, 0};
static const int WIDTH[4] = {24, 23, 11, 14};

static int pos[4][24];   /* (codeword, bit) -> position in the 72-bit frame */
static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { if (fails++ < 20) { printf("  FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)

static void flip(uint8_t fr[9], int p) { fr[p >> 3] ^= (uint8_t)(0x80 >> (p & 7)); }
static int bit(const uint8_t fr[9], int p) { return (fr[p >> 3] >> (7 - (p & 7))) & 1; }

static uint32_t codeword(const uint8_t fr[9], int c) {
    uint32_t v = 0;
    int b;
    for (b = 0; b < WIDTH[c]; b++) v |= (uint32_t)bit(fr, pos[c][b]) << b;
    return v;
}

static int popcnt(uint32_t v) { int n = 0; for (; v; v &= v - 1) n++; return n; }

static void hex2bytes(const char *h, uint8_t *b) {
    int i, n = (int)strlen(h) / 2;
    for (i = 0; i < n; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); b[i] = (uint8_t)v; }
}

static unsigned long rng = 12345;
static unsigned rnd(void) { rng = rng * 1103515245UL + 12345UL; return (unsigned)(rng >> 16) & 0x7FFF; }

/* decode fr (hard and soft) and compare with the expected bits and per-codeword corrections */
static void expect_decode(const uint8_t fr[9], const uint8_t want[49], int e0, int e1, const char *what) {
    uint8_t got[49], e[4];
    int8_t llr[72];
    int r, p;
    r = blip25_fec_3600(fr, got, e);
    CHECK(r == e0 + e1 && e[0] == e0 && e[1] == e1 && !memcmp(got, want, 49),
          "%s: hard decode returned %d (c0 %d, c1 %d), expected %d + %d%s", what, r, e[0], e[1], e0, e1,
          memcmp(got, want, 49) ? ", wrong bits" : "");
    for (p = 0; p < 72; p++) llr[p] = (int8_t)(bit(fr, p) ? 64 : -64);
    blip25_fec_3600_soft(llr, got, e);
    CHECK(!memcmp(got, want, 49), "%s: soft decode gave wrong bits", what);
}

int main(int argc, char **argv) {
    static const char *chip[][2] = {   /* rate 34 -> rate 33 measured on the AMBE-3000R (docs/encoder.md) */
        {"cd4ac301b6e600", "954be6500310b00777"}, {"8d4ac009a64d00", "a478e4520114c02767"},
        {"deba0301a6e600", "fdc5825757247cc3af"}, {"9f9ac301a64500", "eec4a07775103c87ab"}};
    uint8_t r34[7], r33[9], fr[9], bits[49], dec[49], e4[4];
    long w24[25] = {0}, w23[24] = {0};
    int i, s, c, b, v, n, frame;
    const char *vec = argc > 1 ? argv[1] : "../decoder/tests/vectors/chip3600.bin";

    for (s = 0; s < 36; s++) { pos[rW[s]][rX[s]] = 2 * s; pos[rY[s]][rZ[s]] = 2 * s + 1; }

    /* 1. the chip */
    ambe_enc_to_rate33(AMBE_ENC_DMR_SILENCE_RATE34, r33);
    CHECK(!memcmp(r33, AMBE_ENC_DMR_SILENCE_RATE33, 9), "DMR silence frame");
    ambe_enc_bits_to_rate33((ambe_enc_to_ambe_d(AMBE_ENC_DMR_SILENCE_RATE34, bits), bits), r33);
    CHECK(!memcmp(r33, AMBE_ENC_DMR_SILENCE_RATE33, 9), "DMR silence frame from ambe_d bits");
    for (i = 0; i < 4; i++) {
        hex2bytes(chip[i][0], r34);
        hex2bytes(chip[i][1], fr);
        ambe_enc_to_rate33(r34, r33);
        CHECK(!memcmp(r33, fr, 9), "chip frame %s -> %s", chip[i][0], chip[i][1]);
    }
    {
        FILE *f = fopen(vec, "rb");
        long clean = 0, total = 0;
        if (!f) { perror(vec); return 1; }
        while (fread(fr, 1, 9, f) == 9) {
            total++;
            if (blip25_fec_3600(fr, bits, NULL) != 0) continue;
            clean++;
            ambe_enc_bits_to_rate33(bits, r33);
            CHECK(!memcmp(r33, fr, 9), "chip vector frame %ld does not re-encode to itself", total - 1);
        }
        fclose(f);
        printf("chip: DMR silence, 4 measured frames, %ld/%ld error-free frames of %s re-encode bit-exactly\n",
               clean, total, vec);
    }

    /* 2. weight distributions over all 4096 messages */
    for (v = 0; v < 4096; v++) {
        uint8_t z[9];
        memset(bits, 0, 49);
        for (b = 0; b < 12; b++) bits[b] = (uint8_t)((v >> (11 - b)) & 1);
        ambe_enc_bits_to_rate33(bits, fr);
        w24[popcnt(codeword(fr, 0))]++;
        CHECK((codeword(fr, 0) >> 12) == (uint32_t)v, "c0 is not systematic for u0 = %d", v);
        memset(bits, 0, 49);
        ambe_enc_bits_to_rate33(bits, z);   /* u0 = 0: c1 = Golay(u1) xor the PN mask of u0 = 0 */
        for (b = 0; b < 12; b++) bits[12 + b] = (uint8_t)((v >> (11 - b)) & 1);
        ambe_enc_bits_to_rate33(bits, fr);
        w23[popcnt(codeword(fr, 1) ^ codeword(z, 1))]++;
    }
    CHECK(w24[0] == 1 && w24[8] == 759 && w24[12] == 2576 && w24[16] == 759 && w24[24] == 1,
          "c0 weight distribution (want 1 / 759 / 2576 / 759 / 1 at 0 / 8 / 12 / 16 / 24)");
    CHECK(w23[0] == 1 && w23[7] == 253 && w23[8] == 506 && w23[11] == 1288 && w23[12] == 1288 && w23[15] == 506 &&
              w23[16] == 253 && w23[23] == 1,
          "c1 weight distribution (want 1 / 253 / 506 / 1288 / 1288 / 506 / 253 / 1 at 0 / 7 / 8 / 11 / 12 / 15 / 16 / 23)");
    printf("code: c0 = extended Golay [24,12] (d = 8), c1 = Golay [23,12] (d = 7): weight distributions match\n");

    /* 3. error correction (random frames plus all-zero / all-one) */
    for (frame = 0; frame < 40; frame++) {
        long t = 0;
        for (i = 0; i < 49; i++) bits[i] = (uint8_t)(frame == 0 ? 0 : frame == 1 ? 1 : rnd() & 1);
        ambe_enc_bits_to_rate33(bits, r33);
        expect_decode(r33, bits, 0, 0, "clean");
        for (c = 0; c < 2; c++) {   /* every pattern of 1..3 errors in c0 and c1, and of 4 errors in c0 */
            uint32_t m;
            for (m = 1; m < (1u << WIDTH[c]); m++) {
                int k = popcnt(m);
                if (k > (c == 0 ? 4 : 3)) continue;
                memcpy(fr, r33, 9);
                for (b = 0; b < WIDTH[c]; b++)
                    if ((m >> b) & 1) flip(fr, pos[c][b]);
                if (k <= 3) {
                    expect_decode(fr, bits, c == 0 ? k : 0, c == 1 ? k : 0, c ? "c1" : "c0");
                } else {
                    n = blip25_fec_3600(fr, dec, e4);
                    CHECK(n == -1 && e4[0] == 255, "4 errors in c0 not detected");
                }
                t++;
            }
        }
        for (i = 0; i < 200; i++) {   /* up to 3 + 3 errors in c0 and c1 at once, plus errors in c2 / c3 */
            int k0 = rnd() % 4, k1 = rnd() % 4, used[2][24] = {{0}}, j;
            uint8_t want[49];
            memcpy(fr, r33, 9);
            memcpy(want, bits, 49);
            for (c = 0; c < 2; c++)
                for (j = 0; j < (c ? k1 : k0);) {
                    b = (int)(rnd() % WIDTH[c]);
                    if (used[c][b]) continue;
                    used[c][b] = 1;
                    flip(fr, pos[c][b]);
                    j++;
                }
            b = (int)(rnd() % 11);   /* c2 bit b (LSB = 0) is ambe_d bit 34 - b */
            flip(fr, pos[2][b]);
            want[34 - b] ^= 1;
            b = (int)(rnd() % 14);   /* c3 bit b is ambe_d bit 48 - b */
            flip(fr, pos[3][b]);
            want[48 - b] ^= 1;
            n = blip25_fec_3600(fr, dec, e4);
            CHECK(n == k0 + k1 && e4[0] == k0 && e4[1] == k1 && !memcmp(dec, want, 49),
                  "%d + %d errors in c0 / c1 with errors in c2 / c3", k0, k1);
            t++;
        }
        if (frame == 0) printf("errors: %ld patterns per frame", t);
    }
    printf(" x 40 frames: 1..3 errors in c0 / c1 corrected (hard and soft), 4 in c0 detected, c2 / c3 unprotected\n");

    printf(fails ? "fec: %d FAILURES\n" : "fec: all checks passed\n", fails);
    return fails != 0;
}
