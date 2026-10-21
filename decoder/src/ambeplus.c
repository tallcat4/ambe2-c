/* AMBE+ (AMBE-2000 family) frame decoder on the blip25 engine: AMBE-3000R RATET 31 (2000 bps), 0 / 22 (2400 bps,
 * D-STAR), 17 / 26 (4000 bps).
 *
 * Input: the speech bits in the chip's serial (priority) order. Every rate carries the same parameters: pitch b0,
 * V/UV b1, gain b2, PRBA vectors b3 (G2-G4) and b4 (G5-G8), and four higher-order coefficient vectors b5..b8.
 * The parameters go through the engine's own amplitude and synthesis code, with the AMBE+ codebooks, the measured
 * pitch law, an absolute gain and the AMBE+ prediction coefficient 0.8.
 *
 * 2400 bps: codebooks from mbelib (checked against the chip), gain measured on the chip.
 * 2000 bps: the 2400 bps fields without their LSB (b1 complete); pitch and gain sit half way between the two
 *           2400 bps values, the vectors use the even 2400 bps entries.
 * 4000 bps: PRBA and gain books shared with 2400 bps, own 5-bit HOC books (fitted to the chip), six-band voicing,
 *           L up to ~3780 Hz, plus refinements that stay out of the prediction memory (cf. US 6,377,916):
 *           first-half-frame interpolation weights (bits 38-39) and a second quantization layer (bits 40-49: a gain
 *           term per half-frame + VQ of DCT coefficients of the spectral error). Bits 66-78 (a further, apparently
 *           adaptive layer) are not decoded. */
#include "b25_internal.h"
#include "ambeplus.h"

#include <math.h>

#define AP_RHO 0x6666 /* spectral prediction coefficient 0.8, Q15 */

/* measured on the AMBE-3000R: f0 = 8000 * 2^(-4.258322 - 0.021770 b0) Hz */
#define AP_W0_A (-4.258322)
#define AP_W0_B (-0.021770)

typedef struct {
    uint8_t n, pos[9];
} ap_field;

typedef struct {
    ap_field b0, b1, b2, b3, b4, hoc[4];
    int vuv6;              /* b1 = six band flags (else an index into AP_VOICING) */
    int half_step;         /* b0, b2 stand for two 2400 bps values (2000 bps) */
    const int16_t *gain;   /* 1 << b2.n entries (half_step: the 2400 bps book) */
    const int16_t *prba24; /* 512 x 3 */
    const int16_t *prba58; /* 128 x 4 */
    const int16_t *hocb;   /* 4 books of hoc_entries x 4 */
    int hoc_entries, hoc_bits;
} ap_layout;

static const ap_layout L2400 = {
    {7, {0, 1, 2, 3, 4, 5, 47}}, {4, {37, 38, 39, 40}}, {6, {6, 7, 8, 9, 41, 42}},
    {9, {10, 11, 12, 13, 14, 15, 16, 43, 44}}, {7, {17, 18, 19, 20, 21, 45, 46}},
    {{4, {22, 23, 24, 25}}, {4, {26, 27, 28, 29}}, {4, {30, 31, 32, 33}}, {3, {34, 35, 36}}},
    0, 0, AP_GAIN64, AP_PRBA24, AP_PRBA58, AP_HOC, 16, 4};

/* 2000 bps frames are first spread into the 2400 bps layout (blip25.c), dropped LSBs 0 */
static const ap_layout L2000 = {
    {7, {0, 1, 2, 3, 4, 5, 47}}, {4, {37, 38, 39, 40}}, {6, {6, 7, 8, 9, 41, 42}},
    {9, {10, 11, 12, 13, 14, 15, 16, 43, 44}}, {7, {17, 18, 19, 20, 21, 45, 46}},
    {{4, {22, 23, 24, 25}}, {4, {26, 27, 28, 29}}, {4, {30, 31, 32, 33}}, {3, {34, 35, 36}}},
    0, 1, AP_GAIN64, AP_PRBA24, AP_PRBA58, AP_HOC, 16, 4};

static const ap_layout L4000 = {
    {7, {0, 1, 2, 3, 4, 5, 79}}, {6, {50, 51, 52, 53, 54, 55}}, {6, {6, 7, 8, 9, 56, 57}},
    {9, {10, 11, 12, 13, 14, 15, 16, 58, 59}}, {7, {17, 18, 19, 20, 21, 60, 61}},
    {{5, {22, 23, 24, 25, 62}}, {5, {26, 27, 28, 29, 63}}, {5, {30, 31, 32, 33, 64}}, {5, {34, 35, 36, 37, 65}}},
    1, 0, AP_GAIN64, AP_PRBA24, AP_PRBA58, AP4_HOC, 32, 5};


static int field(const int16_t *s, const ap_field *f) {
    int v = 0, i;
    for (i = 0; i < f->n; i++) v = (v << 1) | (s[f->pos[i]] & 1);
    return v;
}

static void put(int16_t *bits, int32_t *at, int v, int n) {
    int i;
    for (i = n - 1; i >= 0; i--) bits[(*at)++] = (int16_t)((v >> i) & 1);
}

/* six voicing bands onto the engine's eight: band k (500 Hz wide) takes the flag of the six-band band holding its
 * centre frequency */
static uint32_t vuv6_pattern(int flags) {
    static const uint8_t MAP[8] = {0, 1, 1, 2, 3, 4, 4, 5};
    uint32_t p = 0;
    int band;
    for (band = 0; band < 8; band++)
        if ((flags >> (5 - MAP[band])) & 1) p |= (1u << (2 * (7 - band))) | (1u << (2 * (15 - band)));
    return p;
}

/* 4000 bps first-half-frame interpolation (bits 38-39): the engine's first half-frame envelope is the mean of the
 * previous and the current one; the index moves it towards the current envelope (a) and adds a gain term (g),
 * both Q12 */
const int16_t AP4_INTERP[8] = {-1024, 0, -512, 0, 0, 128, 1536, 0}; /* fitted on speech */

static void interp(int16_t *synth, const int16_t *cur, const int16_t *s) {
    static const ap_field I = {2, {38, 39}};
    int L = synth[DEC_P_L] < cur[DEC_P_L] ? synth[DEC_P_L] : cur[DEC_P_L], l, i = field(s, &I);
    for (l = 0; l < L && l < 56; l++) {
        double d = AP4_INTERP[2 * i] / 4096.0 * (cur[8 + l] - synth[8 + l]) + AP4_INTERP[2 * i + 1];
        synth[8 + l] = (int16_t)lrint(fmax(-32768.0, fmin(32767.0, synth[8 + l] + d)));
    }
}

/* 4000 bps second quantization layer: a gain term and DCT coefficients 1..10 of the log2 spectral error over the
 * L harmonics, added to the synthesis parameters only (not to the prediction memory) */
static void layer2(int16_t *params, const int16_t *s, int half) {
    static const ap_field G = {3, {40, 41, 42}}, V = {7, {43, 44, 45, 46, 47, 48, 49}};
    int L = params[DEC_P_L], l, k, g = field(s, &G), v = field(s, &V);
    const int16_t *gain = half ? AP4_L2_GAIN_B : AP4_L2_GAIN;
    double vq_scale = AP4_L2_SCALE[half] / 4096.0;
    for (l = 0; l < L && l < 56; l++) {
        double d = 0;
        for (k = 1; k <= 10; k++) d += AP4_L2_VQ[10 * v + k - 1] * cos(3.14159265358979 * k * (l + 0.5) / L);
        d = gain[g] + vq_scale * d;
        params[8 + l] = (int16_t)lrint(fmax(-32768.0, fmin(32767.0, params[8 + l] + d)));
    }
}

/* b25_code_amplitudes (decode direction) with the AMBE+ books and prediction coefficient */
static void ap_code_amplitudes(const ap_layout *lay, const int16_t *prev, int16_t *cur, int16_t gain, int16_t *bits,
                               int32_t *pos, const int16_t *block_lens, const int16_t *cos_tbl) {
    uint16_t l = (uint16_t)cur[2];
    int16_t pred[56] = {0};
    int32_t sum;
    uint16_t packed;
    int16_t lens[4] = {0};
    int16_t prba[8] = {0};
    size_t base, k, i;
    int16_t out[0x38];
    int16_t mags[0x38];

    sum = b25_resample_lerp(pred, l, prev + 8, prev[2]);
    b25_scale_bias_vec(pred, sum, (int16_t)l, AP_RHO);

    packed = (uint16_t)block_lens[(size_t)w16_sub((int16_t)l, 9)];
    for (k = 0; k < 4; k++) {
        lens[k] = w16_add((int16_t)(packed & 0xf), 2);
        packed = (uint16_t)((int16_t)packed >> 4);
    }
    prba[0] = 0;
    b25_codebook_field(prba + 1, 3, 9, lay->prba24, 9, 3, bits, pos, 0);
    b25_codebook_field(prba + 4, 4, 7, lay->prba58, 7, 4, bits, pos, 0);

    base = 0;
    for (k = 0; k < 4; k++) {
        int16_t coeffs = w16_sub(lens[k], 2);
        int16_t avail = lay->hoc[k].n;
        if (coeffs < 1) {
            b25_read_field_i16(bits, pos, avail);
        } else {
            uint16_t nwords = (uint16_t)(coeffs > 4 ? 4 : coeffs);
            b25_codebook_field(cur + 8 + base + 2, nwords, avail, lay->hocb + 4 * lay->hoc_entries * k,
                               (int16_t)lay->hoc_bits, 4, bits, pos, 0);
        }
        base = base + (size_t)lens[k];
    }

    memcpy(out, cur + 8, 0x38 * sizeof(int16_t));
    memcpy(mags, out, 0x38 * sizeof(int16_t));
    b25_expand_prba_to_log_mags(out, l, prba, mags, lens, gain, cos_tbl);
    for (i = 0; i < 0x38; i++) cur[8 + i] = i < (size_t)l ? out[i] : mags[i];
    b25_avg_and_extend(cur + 8, pred, l);
}

int16_t b25_decode_frame_ambeplus(int16_t *cur, int16_t *synth, int16_t *prev, const int16_t *s, const int16_t *ctrl,
                                  int16_t allow_high, int16_t rate, const int16_t *block_lens, const int16_t *cos_tbl) {
    const ap_layout *lay = rate == B25_RATE_AMBEPLUS4000 ? &L4000 : rate == B25_RATE_AMBEPLUS2000 ? &L2000 : &L2400;
    int16_t bits[64];
    int32_t at = 0, pos = 0;
    int b0 = field(s, &lay->b0), b1 = field(s, &lay->b1), b2 = field(s, &lay->b2), k;
    uint32_t m_synth, m_cur;
    int16_t gain;
    int16_t log_pitch;

    if (b0 >= 124) { /* tone / silence / erasure codes: not handled yet */
        prev[DEC_S_TRACK] = -3;
        return 4;
    }
    synth[DEC_P_CLASS] = 1;
    cur[DEC_P_CLASS] = 1;
    synth[DEC_P_TAG] = 0xff;
    cur[DEC_P_TAG] = 0xff;

    b25_expand_dibits(&m_synth, &m_cur, lay->vuv6 ? vuv6_pattern(b1) : AP_VOICING[b1]);
    b25_put32_al4b1(synth, DEC_P_VOICE, m_synth);
    b25_put32_al4b1(cur, DEC_P_VOICE, m_cur);

    log_pitch = (int16_t)lrint((AP_W0_A + AP_W0_B * (b0 + (lay->half_step ? 0.5 : 0.0))) * 4096.0);
    b25_fundamental_from_log2(&cur[DEC_P_W0], &cur[DEC_P_L], log_pitch, allow_high);
    if (lay->vuv6) { /* 4000 bps synthesises up to ~3780 Hz (2400 bps: ~3700 Hz) */
        double f0 = pow(2.0, AP_W0_A + AP_W0_B * b0);
        int l = (int)(0.4725 / f0);
        cur[DEC_P_L] = (int16_t)(l > 56 ? 56 : l);
    }

    /* the AMBE+ gain is absolute: gamma = book[b2], no prediction from the previous frame */
    gain = lay->half_step ? (int16_t)((lay->gain[b2 & ~1] + lay->gain[b2 | 1]) / 2) : lay->gain[b2];
    if (gain > 0x67ff) gain = 0x6800;
    prev[0x42] = gain;

    put(bits, &at, field(s, &lay->b3), 9);
    put(bits, &at, field(s, &lay->b4), 7);
    for (k = 0; k < 4; k++) put(bits, &at, field(s, &lay->hoc[k]), lay->hoc[k].n);
    ap_code_amplitudes(lay, prev, cur, gain, bits, &pos, block_lens, cos_tbl);
    b25_suppress_spectral_peaks(cur, prev, ctrl[DEC_C_FRAMES], ctrl[DEC_C_SUBMODE]);

    b25_select_repeat_params(synth, cur, prev);
    b25_copy_params_batch2(prev, cur, 0);
    /* 4000 bps refinements of the half-frame parameters; prev already holds the prediction memory. The second layer's
     * gain terms and VQ scales per half-frame are fitted on speech (the first half-frame carries most of it) */
    if (lay == &L4000) {
        interp(synth, cur, s);
        layer2(synth, s, 0);
        layer2(cur, s, 1);
    }
    prev[DEC_S_TRACK] = -3;
    return 0;
}
