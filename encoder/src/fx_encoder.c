/* Fixed-point streaming encoder (build with -DAMBE_ENC_FIXED, instead of encoder.c / analysis.c / quant.c / fft.c):
 * the same algorithm and API as encoder.c, in integer arithmetic only. */
#include <string.h>

#include "ambe_enc.h"
#include "fx_internal.h"

struct ambe_encoder {
    int16_t hist[HIST];       /* past input; the current chunk occupies the last N samples */
    long t;                   /* chunks pushed */
    int32_t cost[NCAND];      /* Viterbi accumulated cost (relative to its minimum), Q16 */
    int prov;                 /* provisional pitch of the newest frame */
    fx_an an[2];              /* analysis of the previous [0] and the newest [1] frame */
    int32_t prov_x[VF], prov_raw[9];
    int32_t level_acc[3];
    int32_t floor[9];
    int have_floor;
    int32_t rows[3][VF];      /* decided rows of the last three frames (oldest first) */
    int nrows;
    fxq_state q;
    /* background-noise mode */
    int bg_on;
    long dec_t;
    int32_t lvl_hist[51];
    int nlvl;
    int32_t band_prev[16];
    int rate33;               /* ambe_enc_set_rate(33): 3600x2450 frames */
};

unsigned long ambe_enc_size(void) { return (unsigned long)sizeof(ambe_encoder); }

ambe_encoder *ambe_enc_init(void *mem) {
    ambe_encoder *e = (ambe_encoder *)mem;
    memset(e, 0, sizeof(*e));
    fxq_init(&e->q);
    return e;
}

void ambe_enc_set_background(ambe_encoder *e, int on) { e->bg_on = on != 0; }

/* the fixed-point version has the AMBE+2 rates only */
int ambe_enc_set_rate(ambe_encoder *e, int ratet) {
    if (ratet != 34 && ratet != 33) return 0;
    e->rate33 = ratet == 33;
    return e->rate33 ? 9 : 7;
}

/* a rate-34 frame in out[0..7) -> the frame of the selected rate */
static void to_rate(const ambe_encoder *e, uint8_t *out) {
    uint8_t fr[7];
    if (!e->rate33) return;
    memcpy(fr, out, 7);
    ambe_enc_to_rate33(fr, out);
}

static int32_t floored(const ambe_encoder *e, int i, int32_t raw) {
    return e->have_floor && e->floor[i] + FX_FLOOR_RISE < raw ? e->floor[i] + FX_FLOOR_RISE : raw;
}

static void row_of(const ambe_encoder *e, const int32_t *x, const int32_t *raw, int32_t *row) {
    int i;
    memcpy(row, x, 64 * sizeof(int32_t));
    for (i = 0; i < 9; i++) row[64 + i] = raw[i] - floored(e, i, raw[i]);
}

static void update_floor(ambe_encoder *e, const int32_t *raw) {
    int i;
    for (i = 0; i < 9; i++) e->floor[i] = floored(e, i, raw[i]);
    e->have_floor = 1;
}

static void push_decided(ambe_encoder *e, const int32_t *row) {
    if (e->nrows == 3) {
        memmove(e->rows[0], e->rows[1], 2 * sizeof(e->rows[0]));
        e->nrows = 2;
    }
    memcpy(e->rows[e->nrows++], row, sizeof(e->rows[0]));
}

static void context(const ambe_encoder *e, const int32_t *next, int32_t r[4][VF]) {
    int n = e->nrows;
    memcpy(r[0], e->rows[n >= 3 ? n - 3 : 0], sizeof(r[0]));
    memcpy(r[1], e->rows[n >= 2 ? n - 2 : 0], sizeof(r[1]));
    memcpy(r[2], e->rows[n - 1], sizeof(r[2]));
    memcpy(r[3], next, sizeof(r[3]));
}

static void finish(ambe_encoder *e, const fx_an *a, int b0, int b1, uint8_t out[7]) {
    int g, L, c[9], l;
    int32_t y[LMAX];
    uint8_t v[LMAX];
    fxq_state ns;
    if (a->silent) {
        b0 = 119;
        b1 = 16;
    }
    g = b1 >= 18 && b1 <= 31 ? G56 : b0;
    L = FX_L[g];
    for (l = 0; l < L; l++) v[l] = FX_VUV[b1][FX_BAND[g][l]];
    if (a->silent)
        for (l = 0; l < L; l++) y[l] = FX_SILENCE_LOG2;
    else
        fx_harmonic_amplitudes(a->lg[0], g, L, v, y);
    FX_PROF(FXP_AMPLITUDES);
    fxq_quantize(y, L, &e->q, c);
    c[0] = b0;
    c[1] = b1;
    fxq_pack(c, out);
    fxq_decode(c, L, &e->q, &ns);
    e->q = ns;
}

/* ---- background-noise mode (see encoder.c) */
static int32_t bg_mlp(const int16_t *z, const int16_t *W1, const int32_t *M1, const int32_t *B1, const int16_t *W2,
                      const int32_t *M2, const int32_t *B2, const int16_t *W3, const int32_t *M3, const int32_t *B3) {
    int32_t h[FX_BG_HIDDEN], o;
    int16_t hz[FX_BG_HIDDEN];
    fx_layer(W1, FX_BG_IN, M1, B1, z, FX_BG_HIDDEN, 1, h);
    fx_hidden(h, FX_BG_HIDDEN, hz);
    fx_layer(W2, FX_BG_HIDDEN, M2, B2, hz, FX_BG_HIDDEN, 1, h);
    fx_hidden(h, FX_BG_HIDDEN, hz);
    fx_layer(W3, FX_BG_HIDDEN, M3, B3, hz, 1, 0, &o);
    return o;   /* Q8 */
}

static int background(ambe_encoder *e, const fx_an *a, const int32_t r[4][VF], uint8_t out[7]) {
    int32_t x[FX_BG_IN], band[32], env[FX_BG_L], mean = 0, mn;
    int16_t z[FX_BG_IN];
    int i, j, k, c[9], isbg = 0;
    fxq_state zs;
    if (e->nlvl == 51) memmove(e->lvl_hist, e->lvl_hist + 1, 50 * sizeof(int32_t)), e->nlvl = 50;
    e->lvl_hist[e->nlvl++] = r[2][48];
    for (i = 0; i < 16; i++) {   /* 10 log10(mean power + 1) per band, Q16 dB */
        uint64_t s = 0;
        int nb = FX_BG_BANDS[i + 1] - FX_BG_BANDS[i];
        for (k = FX_BG_BANDS[i]; k < FX_BG_BANDS[i + 1]; k++) s += fx_pow(a->lg[0][k]);
        band[i] = fx_mul16(197283, fx_log2_u64(s + 16 * (uint64_t)nb) - fx_log2_u64(16 * (uint64_t)nb));   /* 3.0103 Q16; s in Q4 */
        mean += band[i];
    }
    mean /= 16;
    for (i = 0; i < 16; i++) band[i] -= mean;
    memcpy(band + 16, e->dec_t ? e->band_prev : band, 16 * sizeof(int32_t));
    memcpy(e->band_prev, band, 16 * sizeof(int32_t));
    if (e->bg_on) {
        for (i = 0; i < 4; i++) memcpy(x + i * VF, r[i], VF * sizeof(int32_t));
        for (j = 0; j < 2; j++) {
            mn = e->lvl_hist[e->nlvl - 1];
            for (i = 1; i <= FX_BG_MIN[j] && i <= e->nlvl - 1; i++)
                if (e->lvl_hist[e->nlvl - 1 - i] < mn) mn = e->lvl_hist[e->nlvl - 1 - i];
            x[4 * VF + j] = mn;
        }
        x[4 * VF + 2] = e->dec_t >= FX_BG_START ? 65536 : (int32_t)(e->dec_t * 65536 / FX_BG_START);
        x[4 * VF + 3] = e->dec_t < FX_BG_START / 2 ? 65536 : 0;
        fx_standardise(x, FX_BV_MU, FX_BV_INVSD, FX_BG_IN, z);
        isbg = bg_mlp(z, &FX_BV1_W[0][0], FX_BV1_MULT, FX_BV1_B, &FX_BV2_W[0][0], FX_BV2_MULT, FX_BV2_B,
                      &FX_BV3_W[0][0], FX_BV3_MULT, FX_BV3_B) > 0;
    }
    e->dec_t++;
    FX_PROF(FXP_BACKGROUND);
    if (!isbg) return 0;
    for (i = 0; i < FX_BG_L; i++) {
        int64_t v = (int64_t)FX_BG_SHAPE[i][32] << 16;
        for (k = 0; k < 32; k++) v += (int64_t)FX_BG_SHAPE[i][k] * band[k];
        env[i] = (int32_t)(v >> 16);
    }
    memset(&zs, 0, sizeof(zs));
    zs.L = FX_BG_L;
    fxq_quantize(env, FX_BG_L, &zs, c);   /* b3..b8; b2 is the level below */
    c[0] = 124;
    c[1] = 16;
    fx_standardise(x, FX_BG_MU, FX_BG_INVSD, FX_BG_IN, z);
    {
        int32_t g = bg_mlp(z, &FX_BG1_W[0][0], FX_BG1_MULT, FX_BG1_B, &FX_BG2_W[0][0], FX_BG2_MULT, FX_BG2_B,
                           &FX_BG3_W[0][0], FX_BG3_MULT, FX_BG3_B);   /* Q8 */
        int gi = (g + 128) >> 8;
        if ((g & 255) == 128 && (gi & 1)) gi--;   /* round half to even */
        c[2] = gi < 0 ? 0 : gi > 31 ? 31 : gi;
    }
    fxq_pack(c, out);
    return 1;
}

static int argmin(const int32_t *x, int n) {
    int i, b = 0;
    for (i = 1; i < n; i++)
        if (x[i] < x[b]) b = i;
    return b;
}

int ambe_enc_push(ambe_encoder *e, const int16_t pcm[160], uint8_t *out) {
    int32_t S[NCAND], f[NCAND], m, x[VF], raw[9], row[VF], next[VF], r[4][VF];
    int arg[NCAND], j, b0, prov, b1;
    memmove(e->hist, e->hist + N, (HIST - N) * sizeof(int16_t));
    memcpy(e->hist + HIST - N, pcm, N * sizeof(int16_t));
    e->an[0] = e->an[1];
    fx_analyse_frame(e->hist + HIST - N, &e->an[1], e->level_acc, e->t == 0);
    for (j = 0; j < NCAND; j++) S[j] = -(e->an[1].logits[j] / 20);
    if (e->t++ == 0) {
        memcpy(e->cost, S, sizeof(S));
        e->prov = argmin(e->cost, NCAND);
        fx_voicing_frame(&e->an[1], e->prov, e->prov_x, e->prov_raw);
        return 0;
    }
    for (j = 0; j < NCAND; j++) f[j] = e->cost[j], arg[j] = j;
    for (j = 1; j < NCAND; j++)
        if (f[j - 1] + FX_PITCH_PENALTY < f[j]) f[j] = f[j - 1] + FX_PITCH_PENALTY, arg[j] = arg[j - 1];
    for (j = NCAND - 2; j >= 0; j--)
        if (f[j + 1] + FX_PITCH_PENALTY < f[j]) f[j] = f[j + 1] + FX_PITCH_PENALTY, arg[j] = arg[j + 1];
    for (j = 0; j < NCAND; j++) f[j] += S[j];
    m = f[argmin(f, NCAND)];
    for (j = 0; j < NCAND; j++) e->cost[j] = f[j] - m;
    prov = argmin(e->cost, NCAND);
    b0 = arg[prov];
    FX_PROF(FXP_VITERBI);
    if (b0 == e->prov) {
        row_of(e, e->prov_x, e->prov_raw, row);
        update_floor(e, e->prov_raw);
    } else {
        fx_voicing_frame(&e->an[0], b0, x, raw);
        row_of(e, x, raw, row);
        update_floor(e, raw);
    }
    push_decided(e, row);
    fx_voicing_frame(&e->an[1], prov, e->prov_x, e->prov_raw);
    row_of(e, e->prov_x, e->prov_raw, next);
    context(e, next, r);
    FX_PROF(FXP_VOICING_FEAT);
    b1 = fx_voicing_classify((const int32_t(*)[VF])r);
    FX_PROF(FXP_CLASSIFY);
    e->prov = prov;
    if (!background(e, &e->an[0], (const int32_t(*)[VF])r, out)) finish(e, &e->an[0], b0, b1, out);
    to_rate(e, out);
    FX_PROF(FXP_QUANT);
    return 1;
}

int ambe_enc_flush(ambe_encoder *e, uint8_t *out) {
    int32_t row[VF], r[4][VF];
    int b1;
    if (e->t == 0) return 0;
    row_of(e, e->prov_x, e->prov_raw, row);
    update_floor(e, e->prov_raw);
    push_decided(e, row);
    context(e, row, r);
    b1 = fx_voicing_classify((const int32_t(*)[VF])r);
    if (!background(e, &e->an[1], (const int32_t(*)[VF])r, out)) finish(e, &e->an[1], e->prov, b1, out);
    to_rate(e, out);
    e->t = 0;
    return 1;
}

void ambe_enc_to_ambe_d(const uint8_t frame[7], uint8_t bits[49]) {
    int i;
    for (i = 0; i < 49; i++) bits[FX_RAW_TO_AMBE_D[i]] = (uint8_t)((frame[i >> 3] >> (7 - (i & 7))) & 1);
}

const uint8_t AMBE_ENC_DMR_SILENCE_RATE34[7] = {0xf7, 0x4a, 0xd8, 0x09, 0x26, 0xb4, 0x00};
const uint8_t AMBE_ENC_DMR_SILENCE_AMBE_D[7] = {0xf8, 0x01, 0xa9, 0x9f, 0x8c, 0xe0, 0x80};
const uint8_t AMBE_ENC_DMR_SILENCE_RATE33[9] = {0xb9, 0xe8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2a, 0x6b};

void ambe_enc_background_frame(int gain, uint8_t out[7]) {
    int c[9] = {124, 16, 0, 52, 79, 18, 14, 12, 1};
    c[2] = gain < 0 ? 0 : gain > 31 ? 31 : gain;
    fxq_pack(c, out);
}
