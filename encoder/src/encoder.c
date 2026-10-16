/* Streaming encoder: mirrors lite/lite_encoder.py encode() one 20 ms chunk at a time. */
#include <math.h>
#include <string.h>

#include "ambe_enc.h"
#include "enc_internal.h"

struct ambe_encoder {
    real hist[HIST];          /* past input; the current chunk occupies the last N samples */
    long t;                   /* chunks pushed */
    real cost[NCAND];         /* Viterbi accumulated cost (relative to its minimum) */
    int prov;                 /* provisional pitch of the newest frame */
    frame_an an[2];           /* analysis of the previous [0] and the newest [1] frame */
    real prov_x[VF], prov_raw[9];  /* newest frame's voicing features at its provisional pitch (no floors) */
    real level_acc[3];
    real floor[9];
    int have_floor;
    real rows[3][VF];         /* decided rows of the last three frames (oldest first) */
    int nrows;
    q_state q;
    /* background-noise mode (ambe_enc_set_background) */
    int bg_on;
    long dec_t;               /* index of the next frame to be decided */
    real lvl_hist[51];        /* lvl0 of the last decided frames (newest last) */
    int nlvl;
    real band_prev[16];       /* band log energies of the previous decided frame */
};

unsigned long ambe_enc_size(void) { return (unsigned long)sizeof(ambe_encoder); }

ambe_encoder *ambe_enc_init(void *mem) {
    ambe_encoder *e = (ambe_encoder *)mem;
    memset(e, 0, sizeof(*e));
    q_init(&e->q);
    return e;
}

/* noise floors: follow drops at once, rise by T_FLOOR_RISE per frame */
static void floor_terms(const ambe_encoder *e, const real *raw, real *out) {
    int i;
    for (i = 0; i < 9; i++) {
        real f = raw[i];
        if (e->have_floor && e->floor[i] + T_FLOOR_RISE < f) f = e->floor[i] + T_FLOOR_RISE;
        out[i] = raw[i] - f;
    }
}

static void update_floor(ambe_encoder *e, const real *raw) {
    int i;
    for (i = 0; i < 9; i++) {
        real f = raw[i];
        if (e->have_floor && e->floor[i] + T_FLOOR_RISE < f) f = e->floor[i] + T_FLOOR_RISE;
        e->floor[i] = f;
    }
    e->have_floor = 1;
}

static void row_of(const ambe_encoder *e, const real *x, const real *raw, real *row) {
    memcpy(row, x, 64 * sizeof(real));
    floor_terms(e, raw, row + 64);
}

static void push_decided(ambe_encoder *e, const real *row) {
    if (e->nrows == 3) {
        memmove(e->rows[0], e->rows[1], 2 * sizeof(e->rows[0]));
        e->nrows = 2;
    }
    memcpy(e->rows[e->nrows++], row, sizeof(e->rows[0]));
}

/* context of the newest decided frame: rows t-2, t-1, t (clamped to the first frame) and `next` */
static void context(const ambe_encoder *e, const real *next, real r[4][VF]) {
    int n = e->nrows;
    memcpy(r[0], e->rows[n >= 3 ? n - 3 : 0], sizeof(r[0]));
    memcpy(r[1], e->rows[n >= 2 ? n - 2 : 0], sizeof(r[1]));
    memcpy(r[2], e->rows[n - 1], sizeof(r[2]));
    memcpy(r[3], next, sizeof(r[3]));
}

/* Digital silence, as the AMBE-3000R codes it: b0 = 119, b1 = 16 (all unvoiced) and a flat, very low amplitude
 * target, so the gain goes to its minimum (b2 = 0) and b3..b8 are whatever the closed-loop quantiser needs to cancel
 * the prediction from the previous frames. Decodes to exact zero. */
#define SILENCE_LOG2 ((real)-20)

static void finish(ambe_encoder *e, const frame_an *a, int b0, int b1, uint8_t out[7]) {
    int l56, L, band[LMAX], c[9], l;
    real f0, v[LMAX], y[LMAX];
    q_state ns;
    if (a->silent) {
        b0 = 119;
        b1 = 16;
    }
    l56 = b1 >= 18 && b1 <= 31;
    L = l56 ? T_L56_L : T_L[b0];
    f0 = l56 ? T_L56_F0 : T_W0[b0];
    band_of_harmonics(f0, L, band);
    for (l = 0; l < L; l++) v[l] = T_VUV[b1][band[l]];
    if (a->silent)
        for (l = 0; l < L; l++) y[l] = SILENCE_LOG2;
    else
        harmonic_amplitudes(a->S[0], 2 * (real)3.141592653589793 * f0, L, v, y);
    q_quantize(y, L, &e->q, c);
    c[0] = b0;
    c[1] = b1;
    q_pack(c, out);
    q_decode(c, L, &e->q, &ns);
    e->q = ns;
}

/* ---- background-noise mode: the AMBE-3000R's ECMODE 0x0800 (see docs/encoder.md). A VAD trained on the chip's own decisions
 * replaces non-speech frames with b0 = 124, b1 = 16 frames: b2 = background level, b3..b8 = its spectral shape
 * quantised on a 15-harmonic grid with the predictor switched off. Such frames leave the quantiser state untouched. */
static real bg_mlp(const real *x, const real (*W1)[T_BG_IN], const real *B1, const real (*W2)[T_BG_HIDDEN],
                   const real *B2, const real *W3, real b3) {
    real h1[T_BG_HIDDEN], h2[T_BG_HIDDEN], o = b3;
    int i, j;
    for (j = 0; j < T_BG_HIDDEN; j++) {
        real h = B1[j];
        for (i = 0; i < T_BG_IN; i++) h += W1[j][i] * x[i];
        h1[j] = h > 0 ? h : 0;
    }
    for (j = 0; j < T_BG_HIDDEN; j++) {
        real h = B2[j];
        for (i = 0; i < T_BG_HIDDEN; i++) h += W2[j][i] * h1[i];
        h2[j] = h > 0 ? h : 0;
    }
    for (i = 0; i < T_BG_HIDDEN; i++) o += W3[i] * h2[i];
    return o;
}

static int round_even(real x) {   /* numpy rint */
    real r = R_FLOOR(x + (real)0.5);
    if (r - x == (real)0.5 && R_FMOD(r, (real)2) != 0) r -= 1;
    return (int)r;
}

/* decide whether the newest decided frame (analysis a, context r) is background; if so write its frame */
static int background(ambe_encoder *e, const frame_an *a, const real r[4][VF], uint8_t out[7]) {
    real x[T_BG_IN], band[32], env[T_BG_L], mean = 0, mn;
    int i, j, k, c[9], isbg = 0;
    q_state z;
    if (e->nlvl == 51) memmove(e->lvl_hist, e->lvl_hist + 1, 50 * sizeof(real)), e->nlvl = 50;
    e->lvl_hist[e->nlvl++] = r[2][48];                     /* lvl0 */
    for (i = 0; i < 16; i++) {
        real s = 0;
        for (k = T_BG_BANDS[i]; k < T_BG_BANDS[i + 1]; k++) s += a->S[0][k];
        band[i] = (real)3.0102999566398120 * fast_log2(s / (T_BG_BANDS[i + 1] - T_BG_BANDS[i]) + 1);
        mean += band[i];
    }
    for (i = 0; i < 16; i++) band[i] -= mean / 16;
    memcpy(band + 16, e->dec_t ? e->band_prev : band, 16 * sizeof(real));
    memcpy(e->band_prev, band, 16 * sizeof(real));
    if (e->bg_on) {
        for (i = 0; i < 4; i++) memcpy(x + i * VF, r[i], VF * sizeof(real));
        for (j = 0; j < 2; j++) {
            mn = e->lvl_hist[e->nlvl - 1];
            for (i = 1; i <= T_BG_MIN[j] && i <= e->nlvl - 1; i++)
                if (e->lvl_hist[e->nlvl - 1 - i] < mn) mn = e->lvl_hist[e->nlvl - 1 - i];
            x[4 * VF + j] = mn;
        }
        x[4 * VF + 2] = e->dec_t >= T_BG_START ? 1 : (real)e->dec_t / T_BG_START;
        x[4 * VF + 3] = e->dec_t < T_BG_START / 2;
        isbg = bg_mlp(x, T_BG_VAD_W1_F, T_BG_VAD_B1_F, T_BG_VAD_W2, T_BG_VAD_B2, T_BG_VAD_W3, T_BG_VAD_B3) > 0;
    }
    e->dec_t++;
    if (!isbg) return 0;
    for (i = 0; i < T_BG_L; i++) {
        real v = T_BG_SHAPE[i][32];
        for (k = 0; k < 32; k++) v += T_BG_SHAPE[i][k] * band[k];
        env[i] = v / (real)6.0205999132796240;              /* dB -> log2 amplitude */
    }
    memset(&z, 0, sizeof(z));
    z.L = T_BG_L;
    q_quantize(env, T_BG_L, &z, c);   /* b3..b8; b2 is the level below */
    c[0] = 124;
    c[1] = 16;
    c[2] = round_even(bg_mlp(x, T_BG_GAIN_W1_F, T_BG_GAIN_B1_F, T_BG_GAIN_W2, T_BG_GAIN_B2, T_BG_GAIN_W3, T_BG_GAIN_B3));
    c[2] = c[2] < 0 ? 0 : c[2] > 31 ? 31 : c[2];
    q_pack(c, out);
    return 1;
}

void ambe_enc_set_background(ambe_encoder *e, int on) { e->bg_on = on != 0; }

static int argmin(const real *x, int n) {
    int i, b = 0;
    for (i = 1; i < n; i++)
        if (x[i] < x[b]) b = i;
    return b;
}

int ambe_enc_push(ambe_encoder *e, const int16_t pcm[160], uint8_t out[7]) {
    real S[NCAND], f[NCAND], m, x[VF], raw[9], row[VF], next[VF], r[4][VF];
    int arg[NCAND], j, b0, prov, b1;
    memmove(e->hist, e->hist + N, (HIST - N) * sizeof(real));
    for (j = 0; j < N; j++) e->hist[HIST - N + j] = pcm[j];
    e->an[0] = e->an[1];
    analyse_frame(e->hist + HIST - N, &e->an[1], e->level_acc, e->t == 0);
    for (j = 0; j < NCAND; j++) S[j] = -e->an[1].logits[j] / 20;
    if (e->t++ == 0) {
        memcpy(e->cost, S, sizeof(S));
        e->prov = argmin(e->cost, NCAND);
        voicing_frame(&e->an[1], e->prov, e->prov_x, e->prov_raw);
        return 0;
    }
    /* Viterbi step: L1 distance transform (penalty per code step), then decide the previous frame */
    for (j = 0; j < NCAND; j++) f[j] = e->cost[j], arg[j] = j;
    for (j = 1; j < NCAND; j++)
        if (f[j - 1] + T_PITCH_PENALTY < f[j]) f[j] = f[j - 1] + T_PITCH_PENALTY, arg[j] = arg[j - 1];
    for (j = NCAND - 2; j >= 0; j--)
        if (f[j + 1] + T_PITCH_PENALTY < f[j]) f[j] = f[j + 1] + T_PITCH_PENALTY, arg[j] = arg[j + 1];
    for (j = 0; j < NCAND; j++) f[j] += S[j];
    m = f[argmin(f, NCAND)];
    for (j = 0; j < NCAND; j++) e->cost[j] = f[j] - m;
    prov = argmin(e->cost, NCAND);
    b0 = arg[prov];
    /* previous frame: decided row (reuse its provisional analysis if the pitch did not change) */
    if (b0 == e->prov) {
        row_of(e, e->prov_x, e->prov_raw, row);
        update_floor(e, e->prov_raw);
    } else {
        voicing_frame(&e->an[0], b0, x, raw);
        row_of(e, x, raw, row);
        update_floor(e, raw);
    }
    push_decided(e, row);
    /* newest frame at its provisional pitch: the lookahead row */
    voicing_frame(&e->an[1], prov, e->prov_x, e->prov_raw);
    row_of(e, e->prov_x, e->prov_raw, next);
    context(e, next, r);
    b1 = voicing_classify((const real(*)[VF])r);
    e->prov = prov;
    if (!background(e, &e->an[0], (const real(*)[VF])r, out)) finish(e, &e->an[0], b0, b1, out);
    return 1;
}

int ambe_enc_flush(ambe_encoder *e, uint8_t out[7]) {
    real row[VF], r[4][VF];
    int b1;
    if (e->t == 0) return 0;
    row_of(e, e->prov_x, e->prov_raw, row);
    update_floor(e, e->prov_raw);
    push_decided(e, row);
    context(e, row, r);
    b1 = voicing_classify((const real(*)[VF])r);
    if (!background(e, &e->an[1], (const real(*)[VF])r, out)) finish(e, &e->an[1], e->prov, b1, out);
    e->t = 0;
    return 1;
}

void ambe_enc_to_ambe_d(const uint8_t frame[7], uint8_t bits[49]) {
    int i;
    for (i = 0; i < 49; i++) bits[T_RAW_TO_AMBE_D[i]] = (uint8_t)((frame[i >> 3] >> (7 - (i & 7))) & 1);
}

const uint8_t AMBE_ENC_DMR_SILENCE_RATE34[7] = {0xf7, 0x4a, 0xd8, 0x09, 0x26, 0xb4, 0x00};
const uint8_t AMBE_ENC_DMR_SILENCE_AMBE_D[7] = {0xf8, 0x01, 0xa9, 0x9f, 0x8c, 0xe0, 0x80};
const uint8_t AMBE_ENC_DMR_SILENCE_RATE33[9] = {0xb9, 0xe8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2a, 0x6b};

void ambe_enc_background_frame(int gain, uint8_t out[7]) {
    int c[9] = {124, 16, 0, 52, 79, 18, 14, 12, 1};
    c[2] = gain < 0 ? 0 : gain > 31 ? 31 : gain;
    q_pack(c, out);
}
