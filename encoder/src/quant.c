/* Spectral-amplitude quantiser (spec tables, closed-loop prediction) and the decoder recursion it tracks.
 * Mirrors ambe2450/encoder.py: interp_prev, blocks, dct_block, idct_block, decode_frame, quantize_frame. */
#include <math.h>
#include <string.h>

#include "enc_internal.h"

void q_init(q_state *s) {
    memset(s, 0, sizeof(*s));
    s->L = T_INIT_L;
}

static real mean(const real *x, int n) {
    real s = 0;
    int i;
    for (i = 0; i < n; i++) s += x[i];
    return s / n;
}

/* previous frame's log2 amplitudes resampled onto L harmonics */
static void interp_prev(const q_state *s, int L, real *out) {
    real ext[1 + LMAX + LMAX + 2];
    int i, n = s->L + 1;
    ext[0] = s->log2m[0];
    for (i = 0; i < s->L; i++) ext[1 + i] = s->log2m[i];
    for (i = n; i < n + LMAX + 2; i++) ext[i] = ext[n - 1];
    for (i = 1; i <= L; i++) {
        real k = (real)s->L / (real)L * (real)i;
        int ki = (int)k;
        real d = k - (real)ki;
        out[i - 1] = (1 - d) * ext[ki] + d * ext[ki + 1];
    }
}

/* first min(J, 6) DCT-II coefficients of x[0..J) */
static void dct6(const real *x, int J, real *C) {
    int k, j, n = J < 6 ? J : 6;
    for (k = 0; k < n; k++) {
        real acc = 0;
        for (j = 0; j < J; j++) acc += T_DCTCOS[J][k][j] * x[j];
        C[k] = acc / J;
    }
}

static void idct(const real *C, int nc, int J, real *out) {
    int k, j;
    for (j = 0; j < J; j++) {
        real acc = 0;
        for (k = 0; k < nc; k++) acc += (k == 0 ? 1 : 2) * C[k] * T_DCTCOS[J][k][j];
        out[j] = acc;
    }
}

static const real *hoc_row(int i, int code) {
    switch (i) {
    case 0: return T_HOC1[code];
    case 1: return T_HOC2[code];
    case 2: return T_HOC3[code];
    default: return T_HOC4[code];
    }
}

static const int HOC_N[4] = {32, 16, 16, 8};
#define SQRT2 ((real)1.4142135623730951)

/* PRBA vector G (8) <-> R (8): an 8-point DCT pair */
static void g_to_r(const real *G, real *R) {
    int k, j;
    for (j = 0; j < 8; j++) {
        real acc = 0;
        for (k = 0; k < 8; k++) acc += (k == 0 ? 1 : 2) * G[k] * T_DCT8[k][j];
        R[j] = acc;
    }
}

static void r_to_g(const real *R, real *G) {
    int k, j;
    for (k = 0; k < 8; k++) {
        real acc = 0;
        for (j = 0; j < 8; j++) acc += T_DCT8[k][j] * R[j];
        G[k] = acc / 8;
    }
}

void q_decode(const int *c, int L, const q_state *s, q_state *ns) {
    real G[8] = {0}, R[8], Tl[LMAX], ip[LMAX], C[6];
    real gamma = T_DG[c[2]] + T_GAIN_DECAY * s->gamma;
    int i, a = 0, k;
    for (k = 0; k < 3; k++) G[1 + k] = T_PRBA24[c[3]][k];
    for (k = 0; k < 4; k++) G[4 + k] = T_PRBA58[c[4]][k];
    g_to_r(G, R);
    for (i = 0; i < 4; i++) {
        int J = T_LMPRBL[L][i], n = J < 6 ? J : 6;
        C[0] = (real)0.5 * (R[2 * i] + R[2 * i + 1]);
        C[1] = (R[2 * i] - R[2 * i + 1]) / (2 * SQRT2);
        for (k = 2; k < n; k++) C[k] = hoc_row(i, c[5 + i])[k - 2];
        idct(C, n, J, Tl + a);
        a += J;
    }
    interp_prev(s, L, ip);
    {
        real mip = mean(ip, L), mtl = mean(Tl, L), lg = (real)0.5 * (real)log2((double)L);
        q_state t;
        memset(&t, 0, sizeof(t));
        for (k = 0; k < L; k++) t.log2m[k] = Tl[k] + T_RHO * (ip[k] - mip) + gamma - lg - mtl;
        t.L = L;
        t.gamma = gamma;
        *ns = t;
    }
}

void q_quantize(const real *y, int L, const q_state *s, int *c) {
    real ip[LMAX], r[LMAX], Tl[LMAX], Cs[4][6], R[8], G[8];
    real mip, mr, target, best;
    int i, k, a = 0, n, code;
    interp_prev(s, L, ip);
    mip = mean(ip, L);
    for (k = 0; k < L; k++) r[k] = y[k] - T_RHO * (ip[k] - mip);
    mr = mean(r, L);
    target = mr + (real)0.5 * (real)log2((double)L) - T_GAIN_DECAY * s->gamma;
    for (best = (real)1e30, code = 0, i = 0; i < 32; i++) {
        real d = (real)fabs((double)(T_DG[i] - target));
        if (d < best) { best = d; code = i; }
    }
    c[2] = code;
    for (k = 0; k < L; k++) Tl[k] = r[k] - mr;
    for (i = 0; i < 4; i++) {
        int J = T_LMPRBL[L][i];
        dct6(Tl + a, J, Cs[i]);
        R[2 * i] = Cs[i][0] + SQRT2 * Cs[i][1];
        R[2 * i + 1] = Cs[i][0] - SQRT2 * Cs[i][1];
        a += J;
    }
    r_to_g(R, G);
    for (best = (real)1e30, code = 0, i = 0; i < 512; i++) {
        real d = 0;
        for (k = 0; k < 3; k++) d += (T_PRBA24[i][k] - G[1 + k]) * (T_PRBA24[i][k] - G[1 + k]);
        if (d < best) { best = d; code = i; }
    }
    c[3] = code;
    for (best = (real)1e30, code = 0, i = 0; i < 128; i++) {
        real d = 0;
        for (k = 0; k < 4; k++) d += (T_PRBA58[i][k] - G[4 + k]) * (T_PRBA58[i][k] - G[4 + k]);
        if (d < best) { best = d; code = i; }
    }
    c[4] = code;
    for (i = 0; i < 4; i++) {
        int J = T_LMPRBL[L][i];
        n = (J < 6 ? J : 6) - 2;
        c[5 + i] = 0;
        if (n <= 0) continue;
        for (best = (real)1e30, code = 0, k = 0; k < HOC_N[i]; k++) {
            real d = 0;
            int m;
            for (m = 0; m < n; m++) d += (hoc_row(i, k)[m] - Cs[i][2 + m]) * (hoc_row(i, k)[m] - Cs[i][2 + m]);
            if (d < best) { best = d; code = k; }
        }
        c[5 + i] = code;
    }
}

/* codes b0..b8 -> 7 bytes in AMBE-3000R rate-34 (ThumbDV) bit order */
void q_pack(const int *c, uint8_t out[7]) {
    uint8_t d[49] = {0};
    int f, i;
    for (f = 0; f < 9; f++) {
        int n = T_FIELD_LEN[f];
        for (i = 0; i < n; i++) d[T_FIELD_POS[f][i]] = (uint8_t)((c[f] >> (n - 1 - i)) & 1);
    }
    memset(out, 0, 7);
    for (i = 0; i < 49; i++) out[i >> 3] |= (uint8_t)(d[T_RAW_TO_AMBE_D[i]] << (7 - (i & 7)));
}
