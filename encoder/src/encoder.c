/* Streaming encoder: mirrors lite/lite_encoder.py encode() one 20 ms chunk at a time. */
#include <math.h>
#include <string.h>

#include "ambe_enc.h"
#include "enc_internal.h"
#include "ap_internal.h"

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
    /* output format (ambe_enc_set_rate) */
    int ratet;                /* AMBE-3000R RATET: 34 (default), 33, or an AMBE+ rate */
    int ap_kind;              /* 0 (AMBE+2) or AP_2400 / AP_2000 / AP_4000 */
    real ap_prev_f0;          /* AMBE+ 4000: the previous frame's pitch and whether it had a voiced band */
    int ap_prev_voiced;
};

/* AMBE+ rates: speech bits K, channel bits, convolutionally coded bits */
static const struct { int ratet, kind, K, nch, n; } AP_RATES[5] = {
    {0, AP_2400, 48, 48, 0}, {22, AP_2400, 48, 80, 20}, {31, AP_2000, 40, 40, 0},
    {17, AP_4000, 80, 80, 0}, {26, AP_4000, 80, 128, 36}};

static int ap_rate_index(int ratet) {
    int i;
    for (i = 0; i < 5; i++)
        if (AP_RATES[i].ratet == ratet) return i;
    return -1;
}

unsigned long ambe_enc_size(void) { return (unsigned long)sizeof(ambe_encoder); }

ambe_encoder *ambe_enc_init(void *mem) {
    ambe_encoder *e = (ambe_encoder *)mem;
    memset(e, 0, sizeof(*e));
    q_init(&e->q);
    e->ratet = 34;
    return e;
}

int ambe_enc_set_rate(ambe_encoder *e, int ratet) {
    int i = ap_rate_index(ratet);
    if (ratet == 34 || ratet == 33) {
        e->ratet = ratet;
        e->ap_kind = 0;
        return ratet == 34 ? 7 : 9;
    }
    if (i < 0) return 0;
    e->ratet = ratet;
    e->ap_kind = AP_RATES[i].kind;
    e->bg_on = 0;
    return (AP_RATES[i].nch + 7) / 8;
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

static void ap_finish(ambe_encoder *e, const frame_an *a, int b0, int b1, uint8_t *out);

static void finish(ambe_encoder *e, const frame_an *a, int b0, int b1, uint8_t *out) {
    int l56, L, band[LMAX], c[9], l;
    real f0, v[LMAX], y[LMAX];
    q_state ns;
    if (e->ap_kind) {
        ap_finish(e, a, b0, b1, out);
        return;
    }
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
    if (e->ratet == 33) {
        uint8_t fr[7];
        q_pack(c, fr);
        ambe_enc_to_rate33(fr, out);
    } else {
        q_pack(c, out);
    }
    q_decode(c, L, &e->q, &ns);
    e->q = ns;
}

/* Fitted to the AMBE-3000R's own AMBE+ frames (neural-ambe2 ambeplus/eval_enc_rates.py), per AP kind: the centre of
 * the amplitude window from the frame start (the AMBE+2 analysis uses T_SPEC_CENTER[0] = -208; at the AMBE+ rates
 * the chip's frames come out one frame earlier, its amplitude window a little later than its pitch window) and a log2
 * offset of the amplitudes (the chip's 4000 bps base layer is coded lower). The first half-frame target of 4000 bps
 * is centred half a frame earlier. */
static const int AP_AMP_CENTER[4] = {0, -258, -258, -278};
static const real AP_GAIN_OFF[4] = {0, 0, 0, (real)-0.2};
#define AP_HALF_OFF (-80)

/* ---- AMBE+ rates: the same analysis (pitch track, voicing classes, amplitudes), AMBE+ parameters and books */

/* AMBE+2 voicing class -> AMBE+ voicing field: 2400 / 2000 bps four 1 kHz band pairs (b1 bit 3 = lowest pair),
 * 4000 bps six bands (bit 5 = lowest); also the eight-band pattern the decoder makes of it. A band is voiced when at
 * least half of the AMBE+2 bands it holds are, the lowest one only when AMBE+2 bands 0 and 1 both are: a voiced
 * 0-500 Hz band alone (frequent at voicing onsets and ends, often with a far too low pitch there) made a low buzz the
 * chip does not have. */
static int ap_voicing(int kind, int b1, real *v8) {
    static const uint8_t SIX_OF_8[8] = {0, 1, 1, 2, 3, 4, 4, 5};
    const real *v = T_VUV[b1];
    int k, code = 0;
    if (kind == AP_4000) {
        real acc[6] = {0}, cnt[6] = {0};
        for (k = 0; k < 8; k++) acc[SIX_OF_8[k]] += v[k], cnt[SIX_OF_8[k]] += 1;
        for (k = 0; k < 6; k++)
            if (k == 0 ? v[0] + v[1] >= 2 : acc[k] >= (real)0.5 * cnt[k]) code |= 1 << (5 - k);
        for (k = 0; k < 8; k++) v8[k] = (real)((code >> (5 - SIX_OF_8[k])) & 1);
    } else {
        for (k = 0; k < 4; k++)
            if (v[2 * k] + v[2 * k + 1] >= (k == 0 ? 2 : 1)) code |= 1 << (3 - k);
        for (k = 0; k < 8; k++) v8[k] = (real)((code >> (3 - k / 2)) & 1);
    }
    return code;
}


static void ap_finish(ambe_encoder *e, const frame_an *a, int b0, int b1, uint8_t *out) {
    const int ri = ap_rate_index(e->ratet), kind = e->ap_kind;
    const real *x = e->hist + HIST - (a == &e->an[0] ? 2 : 1) * N;   /* start of the frame's chunk */
    int L, band[LMAX], c[9], l;
    real f0, v8[8], v[LMAX], y[LMAX], P[NB];
    uint8_t serial[80];
    q_state ns;
    /* digital silence as the AMBE-3000R codes it at the AMBE+ rates: pitch code 34 (2000 bps: 38), all unvoiced, a
     * flat very low amplitude target (gain code 0). The chip gives most unvoiced frames that pitch code too; so do
     * the 2400 and 2000 bps rates here (closer to the chip there, not at 4000 bps). */
    c[1] = ap_voicing(kind, a->silent ? 16 : b1, v8);
    c[0] = a->silent || (c[1] == 0 && kind != AP_4000) ? (kind == AP_2000 ? 38 : 34) : ap_pitch_code(kind, T_W0[b0]);
    f0 = ap_f0(kind, c[0]);
    L = ap_harmonics(kind, c[0]);
    band_of_harmonics(f0, L, band);
    for (l = 0; l < L; l++) v[l] = v8[band[l]];
    if (a->silent)
        for (l = 0; l < L; l++) y[l] = SILENCE_LOG2;
    else {
        amp_spectrum(x, AP_AMP_CENTER[kind], P);
        harmonic_amplitudes(P, 2 * (real)3.141592653589793 * f0, L, v, y);
        for (l = 0; l < L; l++) y[l] += AP_GAIN_OFF[kind];
    }
    ap_quantize(kind, y, L, &e->q, c);
    ap_pack(kind, c, serial);
    ap_decode(kind, c, L, &e->q, &ns);
    if (kind == AP_4000) {
        int rf[7];
        ap_refine_neutral(rf);
        if (!a->silent) { /* split VQ against the first half-frame (between the previous frame and this one) */
            const real f0p = e->ap_prev_f0 > 0 ? e->ap_prev_f0 : f0;
            const real fs = ap_half_f0(f0p, e->ap_prev_voiced, f0, c[1] != 0);
            int Ls = ap_half_harmonics(fs), band2[LMAX];
            real th[LMAX], vh[LMAX];
            band_of_harmonics(fs, Ls, band2);
            for (l = 0; l < Ls; l++) vh[l] = v8[band2[l]];
            amp_spectrum(x, AP_AMP_CENTER[kind] + AP_HALF_OFF, P);
            harmonic_amplitudes(P, 2 * (real)3.141592653589793 * fs, Ls, vh, th);
            for (l = 0; l < Ls; l++) th[l] += AP_GAIN_OFF[kind];
            ap_refine(&e->q, f0p, &ns, f0, fs, th, rf);
        }
        ap_pack_refine(rf, serial);
        e->ap_prev_f0 = f0;
        e->ap_prev_voiced = c[1] != 0;
    }
    ap_channel_encode(serial, AP_RATES[ri].nch, AP_RATES[ri].K, AP_RATES[ri].n, out);
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
static int background(ambe_encoder *e, const frame_an *a, const real r[4][VF], uint8_t *out) {
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
    if (e->ratet == 33) {
        uint8_t fr[7];
        q_pack(c, fr);
        ambe_enc_to_rate33(fr, out);
    } else {
        q_pack(c, out);
    }
    return 1;
}

void ambe_enc_set_background(ambe_encoder *e, int on) { e->bg_on = on != 0 && !e->ap_kind; }

static int argmin(const real *x, int n) {
    int i, b = 0;
    for (i = 1; i < n; i++)
        if (x[i] < x[b]) b = i;
    return b;
}

int ambe_enc_push(ambe_encoder *e, const int16_t pcm[160], uint8_t *out) {
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

int ambe_enc_flush(ambe_encoder *e, uint8_t *out) {
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
