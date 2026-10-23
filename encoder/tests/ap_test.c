/* ap_test DEC_VECTORS_DIR : checks of the AMBE+ encoder against ambe2-c's AMBE+ decoder (../decoder)
 *   1. channel coding: the chip's frames of every AMBE+ rate (decoder test vectors), turned into speech bits by the
 *      decoder, re-encode bit-exactly
 *   2. quantiser model: random parameter frames packed and channel-coded by the encoder and decoded by the decoder
 *      give the decoder the harmonic count and the prediction memory (log2 amplitudes) the encoder's float model
 *      predicts */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blip25.h"
#include "b25_internal.h"
#include "ambeplus.h"
#include "ap_internal.h" /* after the decoder's headers (it defines N) */

static const struct { const char *name; int ratet, kind, K, nch, n; } RATES[5] = {
    {"r0", 0, AP_2400, 48, 48, 0}, {"r22", 22, AP_2400, 48, 80, 20}, {"r31", 31, AP_2000, 40, 40, 0},
    {"r17", 17, AP_4000, 80, 80, 0}, {"r26", 26, AP_4000, 80, 128, 36}};

static int decode(blip25_decoder *d, int ratet, const uint8_t *fr, int16_t *pcm) {
    switch (ratet) {
    case 0: return blip25_decode_ambeplus_r0(d, fr, pcm);
    case 22: return blip25_decode_ambeplus_r22(d, fr, pcm, NULL);
    case 31: return blip25_decode_ambeplus_r31(d, fr, pcm);
    case 17: return blip25_decode_ambeplus_r17(d, fr, pcm);
    default: return blip25_decode_ambeplus_r26(d, fr, pcm, NULL);
    }
}

static int channel_test(const char *dir, int r) {
    char path[512];
    FILE *f;
    int nb = (RATES[r].nch + 7) / 8, frames = 0, same = 0, i;
    uint8_t fr[16], out[16], sp[80];
    int16_t soft[128];
    snprintf(path, sizeof(path), "%s/ap_%s.bin", dir, RATES[r].name);
    if (!(f = fopen(path, "rb"))) { perror(path); return 1; }
    while (fread(fr, 1, (size_t)nb, f) == (size_t)nb) {
        for (i = 0; i < RATES[r].nch; i++) soft[i] = ((fr[i >> 3] >> (7 - (i & 7))) & 1) ? 1 : -1;
        ambeplus_fec_decode(soft, RATES[r].nch, RATES[r].K, RATES[r].n, sp);
        ap_channel_encode(sp, RATES[r].nch, RATES[r].K, RATES[r].n, out);
        frames++;
        same += !memcmp(out, fr, (size_t)nb);
    }
    fclose(f);
    printf("%-4s channel coding: %d/%d chip frames re-encode bit-exactly\n", RATES[r].name, same, frames);
    return same != frames || frames == 0;
}

static int model_test(int r) {
    const int kind = RATES[r].kind, step = kind == AP_2000 ? 2 : 1;
    static blip25_decoder d;
    q_state s, ns;
    int t, i, c[9], fails = 0, nf = 2000;
    double maxerr = 0;
    uint8_t serial[80], fr[16];
    int16_t pcm[160];
    srand(1234 + r);
    blip25_init(&d);
    q_init(&s);
    for (t = 0; t < nf; t++) {
        const int16_t *mem = d.state + DEC_S_PREV_AL8B1;
        int L;
        c[0] = (rand() % 120) / step * step;
        c[1] = rand() % (kind == AP_4000 ? 64 : 16);
        c[2] = rand() % 64 / step * step;
        c[3] = rand() % 512 / step * step;
        c[4] = rand() % 128 / step * step;
        for (i = 0; i < 4; i++) c[5 + i] = rand() % (kind == AP_4000 ? 32 : i == 3 ? 8 : 16) / step * step;
        if (t % 50 == 0) c[2] = 63; /* the decoder clips the gain near the top: keep some frames there */
        L = ap_harmonics(kind, c[0]);
        ap_pack(kind, c, serial);
        if (kind == AP_4000) /* neutral refinement fields */
            for (i = 38; i < 50; i++) serial[i] = 0;
        ap_channel_encode(serial, RATES[r].nch, RATES[r].K, RATES[r].n, fr);
        decode(&d, RATES[r].ratet, fr, pcm);
        ap_decode(kind, c, L, &s, &ns);
        s = ns;
        if (mem[DEC_P_L] != L) {
            if (fails++ < 5) printf("  frame %d: b0 %d, L %d, decoder L %d\n", t, c[0], L, mem[DEC_P_L]);
            continue;
        }
        {
            double fe = 0, mx = 0;
            for (i = 0; i < L; i++) {
                double e = fabs(s.log2m[i] - mem[8 + i] / 2048.0);
                if (e > fe) fe = e;
                if (fabs(s.log2m[i]) > mx) mx = fabs(s.log2m[i]);
            }
            if (fe > maxerr) maxerr = fe;
            if (fe > 0.02 && getenv("AP_TEST_VERBOSE"))
                printf("  frame %d: err %.3f  b0 %d b2 %d L %d max|log2m| %.2f\n", t, fe, c[0], c[2], L, mx);
        }
    }
    printf("%-4s quantiser model: %d frames, L mismatches %d, max |log2 error| %.4f\n", RATES[r].name, nf, fails, maxerr);
    return fails > 0 || maxerr > 0.05;
}

int main(int argc, char **argv) {
    int r, bad = 0;
    const char *dir = argc > 1 ? argv[1] : "../decoder/tests/vectors";
    for (r = 0; r < 5; r++) bad |= channel_test(dir, r);
    for (r = 0; r < 5; r += 1) bad |= model_test(r);
    printf(bad ? "ambeplus: FAILED\n" : "ambeplus: all checks passed\n");
    return bad;
}
