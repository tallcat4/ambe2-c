/* Fixed-point per-frame analysis: spectra, comb features and pitch logits, voicing features and classifier, harmonic
 * amplitudes. Mirrors analysis.c (the float version) step by step. */
#include <string.h>

#include "fx_internal.h"

static const int SHIFTS[5] = {0, -45, 45, -26, 26};
#define AMP_CENTER (-208)
#define SPAN_LO (-472)
#define SPAN_HI (-8)

/* ---- MLP pieces */
void fx_standardise(const int32_t *x, const int32_t *mu, const int32_t *invsd, int n, int16_t *z) {
    int i;
    for (i = 0; i < n; i++) {   /* (x - mu) / sd: Q16 * Q16 -> Q8, clipped to +-8 */
        int64_t v = ((int64_t)(x[i] - mu[i]) * invsd[i]) >> 24;
        z[i] = (int16_t)(v > 2047 ? 2047 : v < -2047 ? -2047 : v);
    }
}

void fx_hidden(const int32_t *h, int n, int16_t *z) {
    int i;
    for (i = 0; i < n; i++) z[i] = (int16_t)(h[i] > 16383 ? 16383 : h[i] < 0 ? 0 : h[i]);
}

/* out[j] = (relu)(sum_i W[j][i] in[i] * scale_j + b_j), in / out Q8. 12-bit weights and inputs clipped to 2^11 (first
 * layers, <= 296 inputs) or 2^14 (hidden, <= 48 inputs) keep |acc| < 2^31. */
void fx_layer(const int16_t *W, int nin, const int32_t *mult, const int32_t *b, const int16_t *in, int nout, int relu,
              int32_t *out) {
    int i, j;
    for (j = 0; j < nout; j++) {
        const int16_t *w = W + j * nin;
        int32_t acc = 0, v;
#if defined(__ARM_FEATURE_DSP)
        for (i = 0; i < nin; i += 8) {   /* two 16-bit products per instruction */
            int32_t w0, w1, w2, w3, x0, x1, x2, x3;
            memcpy(&w0, w + i, 4), memcpy(&w1, w + i + 2, 4), memcpy(&w2, w + i + 4, 4), memcpy(&w3, w + i + 6, 4);
            memcpy(&x0, in + i, 4), memcpy(&x1, in + i + 2, 4), memcpy(&x2, in + i + 4, 4), memcpy(&x3, in + i + 6, 4);
            acc = fx_smlad(w0, x0, acc);
            acc = fx_smlad(w1, x1, acc);
            acc = fx_smlad(w2, x2, acc);
            acc = fx_smlad(w3, x3, acc);
        }
#else
        for (i = 0; i < nin; i += 8) {
            acc += w[i] * in[i];
            acc += w[i + 1] * in[i + 1];
            acc += w[i + 2] * in[i + 2];
            acc += w[i + 3] * in[i + 3];
            acc += w[i + 4] * in[i + 4];
            acc += w[i + 5] * in[i + 5];
            acc += w[i + 6] * in[i + 6];
            acc += w[i + 7] * in[i + 7];
        }
#endif
        v = (int32_t)(((int64_t)acc * mult[j]) >> 16) + b[j];
        out[j] = relu && v < 0 ? 0 : v;
    }
}

/* ---- level */
static int32_t half_log2_mean_sq(const int16_t *x, int n) {   /* 0.5 log2(mean(x^2) + 1) = 0.5 (log2(sum + n) - log2 n) */
    uint64_t s = 0;
    int i;
    for (i = 0; i < n; i++) s += (uint64_t)((int32_t)x[i] * x[i]);
    return (fx_log2_u64(s + (uint64_t)n) - fx_log2_u64((uint64_t)n)) / 2;
}

/* ---- comb features of the four spectra: per candidate, the energy share on its harmonic comb and the comb's mean
 * log2 rise over the spectrum's mean. One pass over the comb table serves all four spectra. Interpolation weights
 * (1 - fr, fr) are Q11 pairs; power relative to the spectrum's maximum is Q14 and log2 Q8 (both < 2^14), and a comb
 * has at most 58 harmonics, so every sum stays below 2^31. With 16-bit dual-MAC instructions (ARM DSP) each
 * interpolation is one load of the bin pair and one instruction. */
static void comb_finish(int b, const int32_t *sp, const int32_t *sl, const uint64_t *rec, const int32_t *meanL,
                        int16_t C[NCAND][NCF]) {
    int k;
    for (k = 0; k < NSPEC; k++) {
        uint64_t sh = ((uint64_t)(uint32_t)sp[k] * rec[k]) >> 43;   /* (sp / 2048) / sumP, Q15 */
        int32_t rise = (int32_t)(((int64_t)sl[k] * FX_COMB_INVN[b]) >> 19) - meanL[k];   /* Q16 */
        rise >>= 8;   /* Q8, clipped to +-32 */
        C[b][2 * k] = (int16_t)(sh > 32767 ? 32767 : sh);
        C[b][2 * k + 1] = (int16_t)(rise > 8192 ? 8192 : rise < -8192 ? -8192 : rise);
    }
}

#if defined(__ARM_FEATURE_DSP)
static inline int32_t pair(const int16_t *p) {   /* p[0] | p[1] << 16 (unaligned load) */
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}
#endif

/* interpolated sums over comb entries o0..o1 of four arrays (4 accumulators: no register spills on small cores) */
#if defined(__ARM_FEATURE_DSP)
static void comb_sums(const int16_t (*x)[NB], int o0, int o1, int32_t *acc) {
    int32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    const int16_t *x0 = x[0], *x1 = x[1], *x2 = x[2], *x3 = x[3];
    int o;
    for (o = o0; o < o1; o++) {
        int i0 = FX_COMB_BIN[o];
        int32_t w = FX_COMB_W[o];
        a0 = fx_smlad(pair(x0 + i0), w, a0);
        a1 = fx_smlad(pair(x1 + i0), w, a1);
        a2 = fx_smlad(pair(x2 + i0), w, a2);
        a3 = fx_smlad(pair(x3 + i0), w, a3);
    }
    acc[0] = a0, acc[1] = a1, acc[2] = a2, acc[3] = a3;
}
#else
static void comb_sums(const int16_t *x, int o0, int o1, int32_t *acc) {   /* x: rows of 8 (interleaved), 4 used */
    int32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    int o;
    for (o = o0; o < o1; o++) {
        const int16_t *u = x + 8 * FX_COMB_BIN[o];
        int32_t w = FX_COMB_W[o], w0 = w & 0xffff, w1 = w >> 16;
        a0 += u[0] * w0;   /* separate statements: one multiply-accumulate each */
        a0 += u[8] * w1;
        a1 += u[1] * w0;
        a1 += u[9] * w1;
        a2 += u[2] * w0;
        a2 += u[10] * w1;
        a3 += u[3] * w0;
        a3 += u[11] * w1;
    }
    acc[0] = a0, acc[1] = a1, acc[2] = a2, acc[3] = a3;
}
#endif

static void comb_all(const int16_t (*lg)[NB], int16_t C[NCAND][NCF]) {
#if defined(__ARM_FEATURE_DSP)
    int16_t P[NSPEC][NB];   /* per spectrum: bin pairs are adjacent, one dual MAC per interpolation */
#define PW(k, i) P[k][i]
#else
    int16_t V[NB][2 * NSPEC];   /* per bin: the four powers, then the four log2 values (one base pointer) */
#define PW(k, i) V[i][k]
#endif
    uint64_t rec[NSPEC];
    int32_t meanL[NSPEC];
    int i, b, k;
    for (k = 0; k < NSPEC; k++) {
        const int16_t *l = lg[k];
        int32_t mx = l[0], sumL = 0, sumP = 0;
        for (i = 1; i < NB; i++)
            if (l[i] > mx) mx = l[i];
        for (i = 0; i < NB; i++) {
            int32_t t = l[i] - mx + 14 * 256;   /* power 2^((lg - max) / 256) in Q14 = 2^(t / 256) */
            PW(k, i) = (int16_t)(t < 0 ? 0 : FX_EXP2_TAB[t & 255] >> (29 - (t >> 8)));
            sumP += PW(k, i);
            sumL += l[i];
#if !defined(__ARM_FEATURE_DSP)
            V[i][NSPEC + k] = l[i];
#endif
        }
        meanL[k] = (sumL * 256 + NB / 2) / NB;   /* Q16 */
        rec[k] = ((uint64_t)1 << 47) / (uint32_t)(sumP + 1);
    }
#undef PW
    for (b = 0; b < NCAND; b++) {
        int32_t sp[NSPEC], sl[NSPEC];
#if defined(__ARM_FEATURE_DSP)
        comb_sums(P, FX_COMB_OFF[b], FX_COMB_OFF[b + 1], sp);
        comb_sums(lg, FX_COMB_OFF[b], FX_COMB_OFF[b + 1], sl);
#else
        comb_sums(&V[0][0], FX_COMB_OFF[b], FX_COMB_OFF[b + 1], sp);
        comb_sums(&V[0][NSPEC], FX_COMB_OFF[b], FX_COMB_OFF[b + 1], sl);
#endif
        comb_finish(b, sp, sl, rec, meanL, C);
    }
}

/* comb feature as Q16 for the MLPs */
static int32_t cq16(const int16_t *c, int j) { return (j & 1) ? (int32_t)c[j] << 8 : (int32_t)c[j] << 1; }

static void pitch_logits(fx_an *a) {
    int32_t x[41], lin[NCAND], h[64];
    int16_t z[48] = {0}, hz[64];
    int b, s, k, j, used[NCAND];
    int32_t lv10 = a->lv / 10;
    int32_t dlv = (int32_t)(((int64_t)FX_PL_W[40] * lv10) >> 16) + FX_PL_B;
    for (b = 0; b < NCAND; b++) {   /* linear prefilter (standardisation folded in): weights Q8, shares Q13, rises Q8;
                                     * |weight| < 34, |share| <= 1, |rise| <= 32: both sums stay below 2^31 */
        int32_t ds = 0, dr = 0;
        for (s = 0; s < 5; s++) {
            int jj = b + SHIFTS[s];
            const int16_t *c;
            const int32_t *w = FX_PL_W8 + s * NCF;
            jj = jj < 0 ? 0 : jj > NCAND - 1 ? NCAND - 1 : jj;
            c = a->C[jj];
            ds += w[0] * (c[0] >> 2) + w[2] * (c[2] >> 2) + w[4] * (c[4] >> 2) + w[6] * (c[6] >> 2);
            dr += w[1] * c[1] + w[3] * c[3] + w[5] * c[5] + w[7] * c[7];
        }
        lin[b] = (ds >> 5) + dr + dlv + FX_PL_BIAS[b] + fx_mul16(a->lv, FX_PL_LEVEL_BIAS[b]);
        a->logits[b] = FX_NOT_CAND;
        used[b] = 0;
    }
    FX_PROF(FXP_PREFILTER);
    for (k = 0; k < TOPK; k++) {   /* MLP on the prefilter's best TOPK (ties: lower index first) */
        int best = -1;
        int64_t y = 0;
        for (b = 0; b < NCAND; b++)
            if (!used[b] && (best < 0 || lin[b] > lin[best])) best = b;
        used[best] = 1;
        for (s = 0; s < 5; s++) {
            int jj = best + SHIFTS[s];
            jj = jj < 0 ? 0 : jj > NCAND - 1 ? NCAND - 1 : jj;
            for (j = 0; j < NCF; j++) x[s * NCF + j] = cq16(a->C[jj], j);
        }
        x[40] = lv10;
        fx_standardise(x, FX_P_MU, FX_P_INVSD, 41, z);
        fx_layer(&FX_P1_W[0][0], 48, FX_P1_MULT, FX_P1_B, z, FX_P_HIDDEN, 1, h);
        fx_hidden(h, FX_P_HIDDEN, hz);
        for (j = 0; j < FX_P_HIDDEN; j++) y += (int64_t)FX_P2_W[j] * hz[j];   /* Q20 */
        a->logits[best] = (int32_t)(y >> 4) + FX_P_B2 + FX_P_BIAS[best] + fx_mul16(a->lv, FX_P_LEVEL_BIAS[best]);
    }
    FX_PROF(FXP_PITCH_MLP);
}

void fx_analyse_frame(const int16_t *x, fx_an *a, int32_t *acc, int first) {
    int32_t lv220;
    int k;
    a->silent = 1;   /* the AMBE-3000R codes a constant (DC-free: zero) input as its silence frame */
    for (k = SPAN_LO + 1; k < SPAN_HI && a->silent; k++) a->silent = x[k] == x[SPAN_LO];
    FX_PROF(FXP_LEVEL);
    fx_fft_lg(x + FX_SPEC_CENTER[0] - 200, FX_WIN_AMP, 400, a->lg[0]);
    fx_fft_lg(x + FX_SPEC_CENTER[1] - 100, FX_HANN200, 200, a->lg[1]);
    fx_fft_lg(x + FX_SPEC_CENTER[2] - 128, FX_HANN256, 256, a->lg[2]);
    fx_fft_lg(x + FX_SPEC_CENTER[3] - 192, FX_HANN384, 384, a->lg[3]);
    FX_PROF(FXP_FFT);
    comb_all((const int16_t(*)[NB])a->lg, a->C);
    FX_PROF(FXP_COMB);
    a->lv = half_log2_mean_sq(x + FX_LEVEL_CENTER - FX_LEVEL_LEN / 2, FX_LEVEL_LEN);
    lv220 = half_log2_mean_sq(x + AMP_CENTER - 110, 220);
    if (first) acc[0] = acc[1] = acc[2] = lv220;
    for (k = 0; k < 3; k++) acc[k] = fx_mul16(FX_LEVEL_DECAYS[k], acc[k]) + fx_mul16(65536 - FX_LEVEL_DECAYS[k], lv220);
    a->lvl[0] = lv220;
    a->lvl[1] = acc[0];
    a->lvl[2] = acc[1];
    a->lvl[3] = acc[2];
    FX_PROF(FXP_LEVEL);
    pitch_logits(a);
}

/* ---- voicing */
static int32_t interp_lg(const int16_t *lg, int32_t pos) {   /* pos Q16 -> Q8 */
    int i0, fr;
    if (pos < 0) pos = 0;
    if (pos > (NB - 1) * 65536 - 66) pos = (NB - 1) * 65536 - 66;
    i0 = pos >> 16;
    fr = (pos >> 4) & 0xfff;
    return lg[i0] + (((lg[i0 + 1] - lg[i0]) * fr) >> 12);
}

static uint64_t interp_pow(const int16_t *lg, int32_t pos) {
    int i0;
    uint32_t fr;
    if (pos < 0) pos = 0;
    if (pos > (NB - 1) * 65536 - 66) pos = (NB - 1) * 65536 - 66;
    i0 = pos >> 16;
    fr = ((uint32_t)pos >> 4) & 0xfff;
    return (fx_pow(lg[i0]) * (4096 - fr) + fx_pow(lg[i0 + 1]) * fr) >> 12;   /* Q4, < 2^62 before the shift */
}

static int32_t ratio_q16(uint64_t num, uint64_t den) {   /* num / den (num <= den), Q16 */
    while (den >= ((uint64_t)1 << 47)) num >>= 1, den >>= 1;
    return (int32_t)((num << 16) / den);
}

void fx_voicing_frame(const fx_an *a, int b0, int32_t *x, int32_t *raw) {
    int32_t k0 = FX_K0[b0], csum[3][8] = {{0}}, be[8], bem = 0;
    uint64_t tot[2][8] = {{0}}, nr[2][8] = {{0}}, sum0 = 0, ben[8] = {0};
    int L = FX_L[b0], cnt[8] = {0}, l, s, k, m, n = 0;
    int32_t lmax;
    const uint8_t *band = FX_BAND[b0];
    for (l = 1; l <= L; l++) cnt[band[l - 1]]++;
    for (s = 0; s < 3; s++)   /* harmonic peak / valley contrast per band */
        for (l = 1; l <= L; l++) {
            int32_t pos = l * k0;
            int32_t va = (interp_lg(a->lg[s], pos - k0 / 2) + interp_lg(a->lg[s], pos + k0 / 2)) / 2;
            csum[s][band[l - 1]] += interp_lg(a->lg[s], pos) - va;
        }
    for (s = 0; s < 3; s++)
        for (k = 0; k < 8; k++) x[n++] = cnt[k] ? csum[s][k] * 256 / cnt[k] : 0;
    for (m = 0; m < NB; m++) {   /* near-harmonic energy share per band, 200- and 256-sample spectra */
        int32_t hq = m * FX_INVK0[b0];   /* m / k0, Q24 */
        int h = (int)(hq >> 24), fr = hq & 0xffffff, d;
        if (fr > 0x800000 || (fr == 0x800000 && (h & 1))) h++;
        if (h < 1 || h > L) continue;
        k = band[h - 1];
        d = m * 65536 - h * k0;
        for (s = 0; s < 2; s++) {
            uint64_t p = fx_pow(a->lg[1 + s][m]);
            tot[s][k] += p;
            if (d <= 98304 && d >= -98304) nr[s][k] += p;
        }
    }
    for (s = 0; s < 2; s++)
        for (k = 0; k < 8; k++) x[n++] = ratio_q16(nr[s][k], tot[s][k] + 16);   /* nr / (tot + 1), powers in Q4 */
    for (l = 1; l <= L; l++) ben[band[l - 1]] += interp_pow(a->lg[0], l * k0);
    for (k = 0; k < 8; k++) {
        be[k] = fx_log2p1(ben[k], -4);
        bem += be[k];
    }
    bem /= 8;
    for (k = 0; k < 8; k++) x[n++] = be[k] - bem;
    for (m = 0; m < NB; m++) sum0 += fx_pow(a->lg[0][m]);
    /* 0.5 log2(sum0 / win2 + 1) = 0.5 (log2(sum0 + win2) - log2 win2), win2 = sum of the squared amp window */
    x[n++] = (fx_log2_u64(sum0 * 16 + FX_WIN2_Q8) - 8 * 65536 - FX_LOG2_WIN2) / 2;   /* sum0 Q4 -> Q8 */
    for (k = 0; k < 4; k++) x[n++] = a->lvl[k];
    for (lmax = a->logits[0], k = 1; k < NCAND; k++)
        if (a->logits[k] > lmax) lmax = a->logits[k];
    x[n++] = lmax - a->logits[b0];
    x[n++] = FX_LOG2F0[b0];
    x[n++] = L * 65536 / 56;
    for (k = 0; k < NCF; k++) x[n++] = cq16(a->C[b0], k);
    raw[0] = x[24 + 16 + 8];
    for (k = 0; k < 8; k++) raw[1 + k] = be[k];
}

int fx_voicing_classify(const int32_t rows[4][VF]) {
    int16_t z[296] = {0}, hz[64];
    int32_t h[64], o[32];
    int j, best = 0;
    fx_standardise(&rows[0][0], FX_V_MU, FX_V_INVSD, 4 * VF, z);
    fx_layer(&FX_V1_W[0][0], 296, FX_V1_MULT, FX_V1_B, z, FX_V_HIDDEN, 1, h);
    fx_hidden(h, FX_V_HIDDEN, hz);
    fx_layer(&FX_V2_W[0][0], FX_V_HIDDEN, FX_V2_MULT, FX_V2_B, hz, FX_V_HIDDEN, 1, h);
    fx_hidden(h, FX_V_HIDDEN, hz);
    fx_layer(&FX_V3_W[0][0], FX_V_HIDDEN, FX_V3_MULT, FX_V3_B, hz, 32, 0, o);
    for (j = 1; j < 32; j++)
        if (o[j] > o[best]) best = j;
    return best;
}

/* ---- harmonic amplitudes: band energies with soft edges (sigmoid table) + learned level terms, log2 Q16 */
void fx_harmonic_amplitudes(const int16_t *lg0, int g, int L, const uint8_t *v, int32_t *y) {
    int32_t k0 = FX_K0[g], invk0 = FX_INVK0[g], lw = FX_LOG2W0[g];
    int l, m;
    for (l = 1; l <= L; l++) {
        uint64_t En = 0;
        int lo = (int)((((int64_t)l * 65536 - FX_HALF_Q16) * k0) >> 32) - 1;
        int hi = (int)((((int64_t)l * 65536 + FX_HALF_Q16) * k0) >> 32) + 2;
        int32_t xt, tv, tu;
        int i0;
        if (lo < 0) lo = 0;
        if (hi > NB) hi = NB;
        for (m = lo; m < hi; m++) {
            int32_t d = m * invk0 - l * (1 << 24);   /* m / k0 - l, Q24 */
            if (d < 0) d = -d;
            if (d < FX_EDGE_LO) En += fx_pow(lg0[m]);
            else if (d < FX_EDGE_HI) {
                uint32_t iq = (uint32_t)(((int64_t)(d - FX_EDGE_LO) * FX_SIG_SCALE) >> 32);   /* Q8 index */
                int i = (int)(iq >> 8);
                int32_t sg = FX_SIG[i] + (((FX_SIG[i + 1] - FX_SIG[i]) * (int32_t)(iq & 255)) >> 8);   /* Q15 */
                En += ((fx_pow(lg0[m]) >> 3) * (uint32_t)sg) >> 12;
            }
        }
        xt = (int32_t)(((int64_t)l * k0) >> 8);   /* w0 l / pi = 2 f0 l, Q16, clamped to 1, on the 64-entry table */
        if (xt > 65536) xt = 65536;
        xt *= 63;
        i0 = xt >> 16;
        if (i0 > 62) i0 = 62;
        xt -= i0 << 16;
        tv = FX_AMP_TAB_V[i0] + fx_mul16(FX_AMP_TAB_V[i0 + 1] - FX_AMP_TAB_V[i0], xt);
        tu = FX_AMP_TAB_U[i0] + fx_mul16(FX_AMP_TAB_U[i0 + 1] - FX_AMP_TAB_U[i0], xt);
        y[l - 1] = fx_log2p1(En, -4) / 2 + (v[l - 1] ? FX_AMP_OFF_V + fx_mul16(FX_AMP_POW_V, lw) + tv
                                                    : FX_AMP_OFF_U + fx_mul16(FX_AMP_POW_U, lw) + tu);
    }
}
