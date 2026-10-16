/* Streaming encoder: mirrors lite/lite_encoder.py encode() one 20 ms chunk at a time. */
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

/* b1 of the newest decided frame from rows t-2, t-1, t (clamped to the first frame) and `next` */
static int classify(const ambe_encoder *e, const real *next) {
    real r[4][VF];
    int n = e->nrows;
    memcpy(r[0], e->rows[n >= 3 ? n - 3 : 0], sizeof(r[0]));
    memcpy(r[1], e->rows[n >= 2 ? n - 2 : 0], sizeof(r[1]));
    memcpy(r[2], e->rows[n - 1], sizeof(r[2]));
    memcpy(r[3], next, sizeof(r[3]));
    return voicing_classify((const real(*)[VF])r);
}

static void finish(ambe_encoder *e, const frame_an *a, int b0, int b1, uint8_t out[7]) {
    int l56 = b1 >= 18 && b1 <= 31, L = l56 ? T_L56_L : T_L[b0], band[LMAX], c[9], l;
    real f0 = l56 ? T_L56_F0 : T_W0[b0], v[LMAX], y[LMAX];
    q_state ns;
    band_of_harmonics(f0, L, band);
    for (l = 0; l < L; l++) v[l] = T_VUV[b1][band[l]];
    harmonic_amplitudes(a->S[0], 2 * (real)3.141592653589793 * f0, L, v, y);
    q_quantize(y, L, &e->q, c);
    c[0] = b0;
    c[1] = b1;
    q_pack(c, out);
    q_decode(c, L, &e->q, &ns);
    e->q = ns;
}

static int argmin(const real *x, int n) {
    int i, b = 0;
    for (i = 1; i < n; i++)
        if (x[i] < x[b]) b = i;
    return b;
}

int ambe_enc_push(ambe_encoder *e, const int16_t pcm[160], uint8_t out[7]) {
    real S[NCAND], f[NCAND], m, x[VF], raw[9], row[VF], next[VF];
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
    b1 = classify(e, next);
    e->prov = prov;
    finish(e, &e->an[0], b0, b1, out);
    return 1;
}

int ambe_enc_flush(ambe_encoder *e, uint8_t out[7]) {
    real row[VF];
    int b1;
    if (e->t == 0) return 0;
    row_of(e, e->prov_x, e->prov_raw, row);
    update_floor(e, e->prov_raw);
    push_decided(e, row);
    b1 = classify(e, row);
    finish(e, &e->an[1], e->prov, b1, out);
    e->t = 0;
    return 1;
}

void ambe_enc_to_ambe_d(const uint8_t frame[7], uint8_t bits[49]) {
    int i;
    for (i = 0; i < 49; i++) bits[T_RAW_TO_AMBE_D[i]] = (uint8_t)((frame[i >> 3] >> (7 - (i & 7))) & 1);
}
