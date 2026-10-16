/* mbelib-compatible AMBE+2 functions on top of blip25c. See include/mbelib.h.
 * The ambe_fr / ambe_d layouts and the processing order follow mbelib 1.3 (ISC, Copyright (C) 2010 mbelib Author):
 * ambe_fr[v][i] is bit i (LSB = 0) of codeword c_v; ambe_d = u0(12) u1(12) u2(11) u3(14), MSB first. */
#include <stdio.h>
#include <string.h>

#include "mbelib.h"

void mbe_printVersion(char *str) { sprintf(str, "%s", MBELIB_VERSION); }

static void init_fields(mbe_parms *mp) {
    int l;
    mp->w0 = 0.09378f;
    mp->L = 30;
    mp->K = 10;
    mp->gamma = 0.0f;
    mp->un = 0;
    mp->repeat = 0;
    for (l = 0; l <= 56; l++) {
        mp->Ml[l] = 0.0f;
        mp->Vl[l] = 0;
        mp->log2Ml[l] = 0.0f;
        mp->PHIl[l] = 0.0f;
        mp->PSIl[l] = 0.0f;
    }
}

void mbe_initMbeParms(mbe_parms *cur_mp, mbe_parms *prev_mp, mbe_parms *prev_mp_enhanced) {
    if (prev_mp) init_fields(prev_mp);
    if (prev_mp_enhanced) init_fields(prev_mp_enhanced);
    init_fields(cur_mp);
    blip25_init(&cur_mp->b25);
}

static uint32_t codeword(char ambe_fr[4][24], int v, int n) {
    uint32_t c = 0;
    int i;
    for (i = 0; i < n; i++) c |= (uint32_t)(ambe_fr[v][i] & 1) << i;
    return c;
}

/* In place, as mbelib: correct c0 (bits 1..23) and return the number of corrected errors (4 = uncorrectable). */
int mbe_eccAmbe3600x2450C0(char ambe_fr[4][24]) {
    uint32_t c[4] = {0, 0, 0, 0};
    uint8_t bits[49], e[4];
    int i;
    c[0] = codeword(ambe_fr, 0, 24);
    blip25_fec_codewords(c, bits, e);
    /* rewrite the corrected info bits u0 (ambe_fr[0][23..12]); parity bits are left as received */
    for (i = 0; i < 12; i++) ambe_fr[0][23 - i] = (char)bits[i];
    return e[0] == 255 ? 4 : e[0];
}

/* In place, as mbelib: XOR c1 with the PN sequence seeded by u0 (taken from ambe_fr[0][23..12]). */
void mbe_demodulateAmbe3600x2450Data(char ambe_fr[4][24]) {
    uint16_t foo = 0, pr;
    int i, k;
    for (i = 23; i >= 12; i--) foo = (uint16_t)((foo << 1) | (ambe_fr[0][i] & 1));
    pr = (uint16_t)(16u * foo);
    for (k = 0; k < 23; k++) {
        pr = (uint16_t)((173u * pr + 13849u) & 0xFFFFu);
        ambe_fr[1][22 - k] = (char)(ambe_fr[1][22 - k] ^ (pr >> 15));
    }
}

/* As mbelib: ambe_d from u0, Golay-decoded (already demodulated) c1, c2, c3; returns the c1 corrections. */
int mbe_eccAmbe3600x2450Data(char ambe_fr[4][24], char *ambe_d) {
    uint32_t c[4];
    uint8_t bits[49], e[4];
    int i;
    /* c0 is already corrected: present it as a clean extended codeword so that only c1 is decoded here; the PN
     * mask that blip25_fec_codewords applies is undone by re-applying it to c1 */
    uint16_t pr, u0 = 0;
    uint32_t mask = 0;
    for (i = 23; i >= 12; i--) u0 = (uint16_t)((u0 << 1) | (ambe_fr[0][i] & 1));
    pr = (uint16_t)(16u * u0);
    for (i = 0; i < 23; i++) {
        pr = (uint16_t)((173u * pr + 13849u) & 0xFFFFu);
        mask |= (uint32_t)(pr >> 15) << (22 - i);
    }
    c[0] = codeword(ambe_fr, 0, 24);
    c[1] = codeword(ambe_fr, 1, 23) ^ mask;
    c[2] = codeword(ambe_fr, 2, 11);
    c[3] = codeword(ambe_fr, 3, 14);
    blip25_fec_codewords(c, bits, e);
    for (i = 0; i < 12; i++) bits[i] = (uint8_t)((u0 >> (11 - i)) & 1); /* u0 exactly as in ambe_fr */
    for (i = 0; i < 49; i++) ambe_d[i] = (char)bits[i];
    return e[1];
}

static int decode(mbe_parms *cur_mp, const char ambe_d[49], int status, int nerr, char *err_str, int16_t pcm[160]) {
    uint8_t bits[49];
    int i, disp;
    for (i = 0; i < 49; i++) bits[i] = (uint8_t)(ambe_d[i] & 1);
    if (err_str) {
        for (i = 0; i < nerr; i++) *err_str++ = '=';
    }
    disp = blip25_decode_bits(&cur_mp->b25, bits, status, pcm);
    if (disp & BLIP25_TONE) {
        if (err_str) *err_str++ = 'T';
        cur_mp->repeat = 0;
    } else if (disp & BLIP25_REPEAT) {
        if (err_str) *err_str++ = 'R';
        cur_mp->repeat++;
    } else {
        cur_mp->repeat = 0;
    }
    if (err_str) *err_str = 0;
    return disp;
}

static void to_float(const int16_t pcm[160], float *aout_buf) {
    int i;
    for (i = 0; i < 160; i++) aout_buf[i] = (float)pcm[i] / 7.0f;
}

static void to_short(const int16_t pcm[160], short *aout_buf) {
    int i;
    for (i = 0; i < 160; i++) aout_buf[i] = (short)(pcm[i] > 32760 ? 32760 : pcm[i] < -32760 ? -32760 : pcm[i]);
}

/* 2450 (FEC removed upstream): *errs2 is taken as the caller's corrected-error count and drives concealment */
void mbe_processAmbe2450Dataf(float *aout_buf, int *errs, int *errs2, char *err_str, char ambe_d[49], mbe_parms *cur_mp,
                              mbe_parms *prev_mp, mbe_parms *prev_mp_enhanced, int uvquality) {
    int16_t pcm[160];
    int n = errs2 ? *errs2 : 0;
    (void)errs, (void)prev_mp, (void)prev_mp_enhanced, (void)uvquality;
    decode(cur_mp, ambe_d, n < 0 ? 0 : n, n < 0 ? 0 : n, err_str, pcm);
    to_float(pcm, aout_buf);
}

void mbe_processAmbe2450Data(short *aout_buf, int *errs, int *errs2, char *err_str, char ambe_d[49], mbe_parms *cur_mp,
                             mbe_parms *prev_mp, mbe_parms *prev_mp_enhanced, int uvquality) {
    int16_t pcm[160];
    int n = errs2 ? *errs2 : 0;
    (void)errs, (void)prev_mp, (void)prev_mp_enhanced, (void)uvquality;
    decode(cur_mp, ambe_d, n < 0 ? 0 : n, n < 0 ? 0 : n, err_str, pcm);
    to_short(pcm, aout_buf);
}

/* 3600x2450: FEC as mbelib (ambe_fr and ambe_d are updated in place), concealment as blip25 */
static void frame3600(int16_t pcm[160], int *errs, int *errs2, char *err_str, char ambe_fr[4][24], char ambe_d[49],
                      mbe_parms *cur_mp) {
    uint32_t c[4] = {0, 0, 0, 0};
    uint8_t bits[49], e[4];
    int lost;
    c[0] = codeword(ambe_fr, 0, 24);
    blip25_fec_codewords(c, bits, e);
    lost = e[0] == 255;
    *errs = mbe_eccAmbe3600x2450C0(ambe_fr);
    mbe_demodulateAmbe3600x2450Data(ambe_fr);
    *errs2 = *errs + mbe_eccAmbe3600x2450Data(ambe_fr, ambe_d);
    decode(cur_mp, ambe_d, lost ? BLIP25_LOST : *errs2, *errs2, err_str, pcm);
}

void mbe_processAmbe3600x2450Framef(float *aout_buf, int *errs, int *errs2, char *err_str, char ambe_fr[4][24],
                                    char ambe_d[49], mbe_parms *cur_mp, mbe_parms *prev_mp,
                                    mbe_parms *prev_mp_enhanced, int uvquality) {
    int16_t pcm[160];
    (void)prev_mp, (void)prev_mp_enhanced, (void)uvquality;
    frame3600(pcm, errs, errs2, err_str, ambe_fr, ambe_d, cur_mp);
    to_float(pcm, aout_buf);
}

void mbe_processAmbe3600x2450Frame(short *aout_buf, int *errs, int *errs2, char *err_str, char ambe_fr[4][24],
                                   char ambe_d[49], mbe_parms *cur_mp, mbe_parms *prev_mp,
                                   mbe_parms *prev_mp_enhanced, int uvquality) {
    int16_t pcm[160];
    (void)prev_mp, (void)prev_mp_enhanced, (void)uvquality;
    frame3600(pcm, errs, errs2, err_str, ambe_fr, ambe_d, cur_mp);
    to_short(pcm, aout_buf);
}

void mbe_synthesizeSilencef(float *aout_buf) { memset(aout_buf, 0, 160 * sizeof(float)); }
void mbe_synthesizeSilence(short *aout_buf) { memset(aout_buf, 0, 160 * sizeof(short)); }

void mbe_floattoshort(float *float_buf, short *aout_buf) {
    int i;
    for (i = 0; i < 160; i++) {
        float a = 7.0f * float_buf[i];
        aout_buf[i] = (short)(a > 32760.0f ? 32760.0f : a < -32760.0f ? -32760.0f : a);
    }
}
