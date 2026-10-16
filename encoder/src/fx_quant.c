/* Fixed-point spectral-amplitude quantiser and the decoder recursion it tracks (log2 domain, Q16).
 * Mirrors quant.c. */
#include <string.h>

#include "fx_internal.h"

#define SQRT2_Q16 92682

void fxq_init(fxq_state *s) {
    memset(s, 0, sizeof(*s));
    s->L = FX_INIT_L;
}

static int32_t mean(const int32_t *x, int n) {
    int64_t s = 0;
    int i;
    for (i = 0; i < n; i++) s += x[i];
    return (int32_t)((s * FX_INV_L[n]) >> 24);
}

/* previous frame's log2 amplitudes resampled onto L harmonics */
static void interp_prev(const fxq_state *s, int L, int32_t *out) {
    int32_t ext[1 + LMAX + LMAX + 2];
    int i, n = s->L + 1;
    ext[0] = s->log2m[0];
    for (i = 0; i < s->L; i++) ext[1 + i] = s->log2m[i];
    for (i = n; i < n + LMAX + 2; i++) ext[i] = ext[n - 1];
    for (i = 1; i <= L; i++) {
        uint32_t kq = (uint32_t)(((uint64_t)(s->L * i) << 16) / (uint32_t)L);   /* s->L / L * i, Q16 */
        int ki = (int)(kq >> 16);
        int32_t d = (int32_t)(kq & 0xffff);
        out[i - 1] = ext[ki] + (int32_t)(((int64_t)(ext[ki + 1] - ext[ki]) * d) >> 16);
    }
}

/* first min(J, 6) DCT-II coefficients of x[0..J) */
static void dct6(const int32_t *x, int J, int32_t *C) {
    int k, j, n = J < 6 ? J : 6;
    for (k = 0; k < n; k++) {
        int64_t acc = 0;
        for (j = 0; j < J; j++) acc += (int64_t)FX_DCTCOS[J][k][j] * x[j];
        C[k] = (int32_t)(((acc >> 14) * FX_INV_L[J]) >> 24);
    }
}

static void idct(const int32_t *C, int nc, int J, int32_t *out) {
    int k, j;
    for (j = 0; j < J; j++) {
        int64_t acc = 0;
        for (k = 0; k < nc; k++) acc += (int64_t)(k == 0 ? C[k] : 2 * C[k]) * FX_DCTCOS[J][k][j];
        out[j] = (int32_t)(acc >> 14);
    }
}

static const int32_t *hoc_row(int i, int code) {
    switch (i) {
    case 0: return FX_HOC1[code];
    case 1: return FX_HOC2[code];
    case 2: return FX_HOC3[code];
    default: return FX_HOC4[code];
    }
}

static const int HOC_N[4] = {32, 16, 16, 8};

static const int16_t *hoc_s(int i) {
    switch (i) {
    case 0: return &FX_HOC1_S[0][0];
    case 1: return &FX_HOC2_S[0][0];
    case 2: return &FX_HOC3_S[0][0];
    default: return &FX_HOC4_S[0][0];
    }
}

/* nearest of `count` codewords (rows of `stride` Q12 values, first n compared) to target t (Q16), in 32-bit
 * arithmetic: the target is taken to Q12 and clipped to +-3, so with |codewords| < 1.7 every squared distance of up
 * to 4 terms stays below 2^31. Ties: lowest index. */
static int search_n(const int16_t *cb, int count, int stride, int n, const int32_t *t) {
    int32_t q[4], best = INT32_MAX, d, e;
    int i, k, code = 0;
    for (k = 0; k < n; k++) {
        int32_t v = t[k] >> 4;
        q[k] = v > 12288 ? 12288 : v < -12288 ? -12288 : v;
    }
    for (i = 0; i < count; i++, cb += stride) {
        for (d = 0, k = 0; k < n; k++) {
            e = cb[k] - q[k];
            d += e * e;
        }
        if (d < best) best = d, code = i;
    }
    return code;
}

static void g_to_r(const int32_t *G, int32_t *R) {
    int k, j;
    for (j = 0; j < 8; j++) {
        int64_t acc = 0;
        for (k = 0; k < 8; k++) acc += (int64_t)(k == 0 ? G[k] : 2 * G[k]) * FX_DCT8[k][j];
        R[j] = (int32_t)(acc >> 14);
    }
}

static void r_to_g(const int32_t *R, int32_t *G) {
    int k, j;
    for (k = 0; k < 8; k++) {
        int64_t acc = 0;
        for (j = 0; j < 8; j++) acc += (int64_t)FX_DCT8[k][j] * R[j];
        G[k] = (int32_t)(acc >> 17);   /* / 2^14 / 8 */
    }
}

void fxq_decode(const int *c, int L, const fxq_state *s, fxq_state *ns) {
    int32_t G[8] = {0}, R[8], Tl[LMAX], ip[LMAX], C[6];
    int32_t gamma = FX_DG[c[2]] + fx_mul16(FX_GAIN_DECAY, s->gamma), mip, mtl;
    int i, a = 0, k;
    fxq_state t;
    for (k = 0; k < 3; k++) G[1 + k] = FX_PRBA24[c[3]][k];
    for (k = 0; k < 4; k++) G[4 + k] = FX_PRBA58[c[4]][k];
    g_to_r(G, R);
    for (i = 0; i < 4; i++) {
        int J = FX_LMPRBL[L][i], n = J < 6 ? J : 6;
        C[0] = (R[2 * i] + R[2 * i + 1]) / 2;
        C[1] = (int32_t)(((int64_t)(R[2 * i] - R[2 * i + 1]) * 46341) >> 17);   /* / (2 sqrt 2) */
        for (k = 2; k < n; k++) C[k] = hoc_row(i, c[5 + i])[k - 2];
        idct(C, n, J, Tl + a);
        a += J;
    }
    interp_prev(s, L, ip);
    mip = mean(ip, L);
    mtl = mean(Tl, L);
    memset(&t, 0, sizeof(t));
    for (k = 0; k < L; k++) t.log2m[k] = Tl[k] + fx_mul16(FX_RHO, ip[k] - mip) + gamma - FX_HALF_LOG2_L[L] - mtl;
    t.L = L;
    t.gamma = gamma;
    *ns = t;
}

void fxq_quantize(const int32_t *y, int L, const fxq_state *s, int *c) {
    int32_t ip[LMAX], r[LMAX], Tl[LMAX], Cs[4][6], R[8], G[8], mip, mr, target;
    int32_t best;
    int i, k, a = 0, n, code;
    interp_prev(s, L, ip);
    mip = mean(ip, L);
    for (k = 0; k < L; k++) r[k] = y[k] - fx_mul16(FX_RHO, ip[k] - mip);
    mr = mean(r, L);
    target = mr + FX_HALF_LOG2_L[L] - fx_mul16(FX_GAIN_DECAY, s->gamma);
    for (best = INT32_MAX, code = 0, i = 0; i < 32; i++) {
        int32_t d = FX_DG[i] - target;
        if (d < 0) d = -d;
        if (d < best) best = d, code = i;
    }
    c[2] = code;
    for (k = 0; k < L; k++) Tl[k] = r[k] - mr;
    for (i = 0; i < 4; i++) {
        int J = FX_LMPRBL[L][i];
        dct6(Tl + a, J, Cs[i]);
        R[2 * i] = Cs[i][0] + fx_mul16(SQRT2_Q16, Cs[i][1]);
        R[2 * i + 1] = Cs[i][0] - fx_mul16(SQRT2_Q16, Cs[i][1]);
        a += J;
    }
    r_to_g(R, G);
    c[3] = search_n(&FX_PRBA24_S[0][0], 512, 3, 3, G + 1);
    c[4] = search_n(&FX_PRBA58_S[0][0], 128, 4, 4, G + 4);
    for (i = 0; i < 4; i++) {
        int J = FX_LMPRBL[L][i];
        n = (J < 6 ? J : 6) - 2;
        c[5 + i] = n <= 0 ? 0 : search_n(hoc_s(i), HOC_N[i], 4, n, Cs[i] + 2);
    }
}

/* codes b0..b8 -> 7 bytes in AMBE-3000R rate-34 (ThumbDV) bit order */
void fxq_pack(const int *c, uint8_t out[7]) {
    uint8_t d[49] = {0};
    int f, i;
    for (f = 0; f < 9; f++) {
        int n = FX_FIELD_LEN[f];
        for (i = 0; i < n; i++) d[FX_FIELD_POS[f][i]] = (uint8_t)((c[f] >> (n - 1 - i)) & 1);
    }
    memset(out, 0, 7);
    for (i = 0; i < 49; i++) out[i >> 3] |= (uint8_t)(d[FX_RAW_TO_AMBE_D[i]] << (7 - (i & 7)));
}
