/* AMBE+ (AMBE-2000 family) parameter quantiser: RATET 0 / 22 (2400 bps), 31 (2000 bps), 17 / 26 (4000 bps).
 * Closed loop on the prediction memory of ambe2-c's AMBE+ decoder (decoder/src/ambeplus.c), which this file mirrors
 * in floating point (within ~0.005 log2 of the decoder's fixed-point engine):
 *   log2 M_l = T_l - mean(T) + gain[b2] - 0.5 log2 L + 0.8 (P_l - mean(P)),   P = previous frame resampled to L
 * with an absolute gain (no prediction), the AMBE+ PRBA / HOC books and the engine's block lengths. */
#include <math.h>
#include <string.h>

#include "ap_internal.h"

#define AP_RHO ((real)0.8)
#define SQRT2 ((real)1.4142135623730951)

/* serial positions of the fields, MSB first (as decoder/src/ambeplus.c) */
typedef struct {
    uint8_t n, pos[9];
} ap_field;

typedef struct {
    ap_field b0, b1, b2, b3, b4, hoc[4];
} ap_layout;

static const ap_layout L2400 = {
    {7, {0, 1, 2, 3, 4, 5, 47}}, {4, {37, 38, 39, 40}}, {6, {6, 7, 8, 9, 41, 42}},
    {9, {10, 11, 12, 13, 14, 15, 16, 43, 44}}, {7, {17, 18, 19, 20, 21, 45, 46}},
    {{4, {22, 23, 24, 25}}, {4, {26, 27, 28, 29}}, {4, {30, 31, 32, 33}}, {3, {34, 35, 36}}}};

static const ap_layout L4000 = {
    {7, {0, 1, 2, 3, 4, 5, 79}}, {6, {50, 51, 52, 53, 54, 55}}, {6, {6, 7, 8, 9, 56, 57}},
    {9, {10, 11, 12, 13, 14, 15, 16, 58, 59}}, {7, {17, 18, 19, 20, 21, 60, 61}},
    {{5, {22, 23, 24, 25, 62}}, {5, {26, 27, 28, 29, 63}}, {5, {30, 31, 32, 33, 64}}, {5, {34, 35, 36, 37, 65}}}};

/* RATET 31: the 2400 bps serial positions that the 40 bits of a 2000 bps frame carry (the LSBs 25, 29, 33, 36, 42,
 * 44, 46 and 47 are dropped, i.e. 0) */
static const uint8_t R31_TO_2400[40] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                        22, 23, 24, 26, 27, 28, 30, 31, 32, 34, 35, 37, 38, 39, 40, 41, 43, 45};

/* f0 (cycles per sample) = 2^(-4.258322 - 0.021770 b0), measured on the AMBE-3000R; 2000 bps codes are even and
 * stand for b0 + 0.5 */
#define AP_W0_A ((real)-4.258322)
#define AP_W0_B ((real)-0.021770)

static real ap_pitch_pos(int kind, int b0) { return (real)b0 + (kind == AP_2000 ? (real)0.5 : 0); }

real ap_f0(int kind, int b0) { return R_EXP2(AP_W0_A + AP_W0_B * ap_pitch_pos(kind, b0)); }

int ap_harmonics(int kind, int b0) {
    real f0 = ap_f0(kind, b0);
    int L = (int)((kind == AP_4000 ? (real)0.4725 : (real)0.4627) / f0);
    if (L > 56) L = 56;
    if (L < 9) L = 9;
    return L;
}

int ap_pitch_code(int kind, real f0) {
    real x = (R_LOG2(f0) - AP_W0_A) / AP_W0_B;
    int b0;
    if (kind == AP_2000) {
        b0 = 2 * (int)R_FLOOR((x - (real)0.5) / 2 + (real)0.5);
        return b0 < 0 ? 0 : b0 > 118 ? 118 : b0;
    }
    b0 = (int)R_FLOOR(x + (real)0.5);
    return b0 < 0 ? 0 : b0 > 119 ? 119 : b0;
}

static real gain_of(int kind, int b2) {
    return kind == AP_2000 ? (T_AP_GAIN[b2 & ~1] + T_AP_GAIN[b2 | 1]) / 2 : T_AP_GAIN[b2];
}

static int hoc_entries(int kind, int i) { return kind == AP_4000 ? 32 : i == 3 ? 8 : 16; }

/* HOC vector of block i for field value code: a 3-bit field reads every second entry of its 16-entry book */
static const real *hoc_row(int kind, int i, int code) {
    if (kind == AP_4000) return T_AP4_HOC[i][code];
    return T_AP_HOC[i][i == 3 ? 2 * code : code];
}

static real mean(const real *x, int n) {
    real s = 0;
    int i;
    for (i = 0; i < n; i++) s += x[i];
    return s / n;
}

/* previous frame's log2 amplitudes resampled onto L harmonics (b25_resample_lerp) */
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

static real dct_cos(int k, int j, int J) { return R_COS((real)3.141592653589793 * (real)k * ((real)j + (real)0.5) / (real)J); }

static void idct(const real *C, int nc, int J, real *out) {
    int k, j;
    for (j = 0; j < J; j++) {
        real acc = 0;
        for (k = 0; k < nc; k++) acc += (k == 0 ? 1 : 2) * C[k] * dct_cos(k, j, J);
        out[j] = acc;
    }
}

static void dct(const real *x, int J, int nc, real *C) {
    int k, j;
    for (k = 0; k < nc; k++) {
        real acc = 0;
        for (j = 0; j < J; j++) acc += dct_cos(k, j, J) * x[j];
        C[k] = acc / J;
    }
}

void ap_decode(int kind, const int *c, int L, const q_state *s, q_state *ns) {
    real G[8] = {0}, R[8], Tl[LMAX], ip[LMAX], C[6];
    int i, a = 0, k;
    for (k = 0; k < 3; k++) G[1 + k] = T_AP_PRBA24[c[3]][k];
    for (k = 0; k < 4; k++) G[4 + k] = T_AP_PRBA58[c[4]][k];
    idct(G, 8, 8, R);
    for (i = 0; i < 4; i++) {
        int J = T_AP_LENS[L][i], n = J < 6 ? J : 6;
        C[0] = (real)0.5 * (R[2 * i] + R[2 * i + 1]);
        C[1] = (R[2 * i] - R[2 * i + 1]) / (2 * SQRT2);
        for (k = 2; k < n; k++) C[k] = hoc_row(kind, i, c[5 + i])[k - 2];
        idct(C, n, J, Tl + a);
        a += J;
    }
    interp_prev(s, L, ip);
    {
        real mip = mean(ip, L), mtl = mean(Tl, L), g = gain_of(kind, c[2]) - (real)0.5 * R_LOG2((real)L);
        q_state t;
        memset(&t, 0, sizeof(t));
        for (k = 0; k < L; k++) { /* clamped as the engine's Q11 memory (b25_avg_and_extend) */
            real v = Tl[k] - mtl + g + AP_RHO * (ip[k] - mip);
            t.log2m[k] = v > (real)(0x77ff / 2048.0) ? (real)(0x77ff / 2048.0) : v < -15 ? -15 : v;
        }
        t.L = L;
        *ns = t;
    }
}

/* nearest entry of an n x dim book to x; step 2 searches only the even entries (2000 bps: LSB dropped) */
static int nearest(const real *book, int n, int dim, const real *x, int step) {
    real best = (real)1e30;
    int i, k, code = 0;
    for (i = 0; i < n; i += step) {
        real d = 0;
        for (k = 0; k < dim; k++) d += (book[i * dim + k] - x[k]) * (book[i * dim + k] - x[k]);
        if (d < best) { best = d; code = i; }
    }
    return code;
}

void ap_quantize(int kind, const real *y, int L, const q_state *s, int *c) {
    real ip[LMAX], r[LMAX], Tl[LMAX], Cs[4][6], R[8], G[8], mip, mr, target, best;
    int i, k, a = 0, step = kind == AP_2000 ? 2 : 1;
    interp_prev(s, L, ip);
    mip = mean(ip, L);
    for (k = 0; k < L; k++) r[k] = y[k] - AP_RHO * (ip[k] - mip);
    mr = mean(r, L);
    target = mr + (real)0.5 * R_LOG2((real)L);
    for (best = (real)1e30, c[2] = 0, i = 0; i < 64; i += step) {
        real d = R_FABS(gain_of(kind, i) - target);
        if (d < best) { best = d; c[2] = i; }
    }
    for (k = 0; k < L; k++) Tl[k] = r[k] - mr;
    for (i = 0; i < 4; i++) {
        int J = T_AP_LENS[L][i];
        dct(Tl + a, J, J < 6 ? J : 6, Cs[i]);
        R[2 * i] = Cs[i][0] + SQRT2 * Cs[i][1];
        R[2 * i + 1] = Cs[i][0] - SQRT2 * Cs[i][1];
        a += J;
    }
    dct(R, 8, 8, G);
    c[3] = nearest(&T_AP_PRBA24[0][0], 512, 3, G + 1, step);
    c[4] = nearest(&T_AP_PRBA58[0][0], 128, 4, G + 4, step);
    for (i = 0; i < 4; i++) {
        int J = T_AP_LENS[L][i], n = (J < 6 ? J : 6) - 2;
        c[5 + i] = 0;
        if (n <= 0) continue;
        for (best = (real)1e30, k = 0; k < hoc_entries(kind, i); k += step) {
            const real *h = hoc_row(kind, i, k);
            real d = 0;
            int m;
            for (m = 0; m < n; m++) d += (h[m] - Cs[i][2 + m]) * (h[m] - Cs[i][2 + m]);
            if (d < best) { best = d; c[5 + i] = k; }
        }
    }
}

static void put(uint8_t *s, const ap_field *f, int v) {
    int i;
    for (i = 0; i < f->n; i++) s[f->pos[i]] = (uint8_t)((v >> (f->n - 1 - i)) & 1);
}

void ap_pack(int kind, const int *c, uint8_t *serial) {
    const ap_layout *lay = kind == AP_4000 ? &L4000 : &L2400;
    uint8_t s[80];
    int i;
    memset(s, 0, sizeof(s));
    put(s, &lay->b0, c[0]);
    put(s, &lay->b1, c[1]);
    put(s, &lay->b2, c[2]);
    put(s, &lay->b3, c[3]);
    put(s, &lay->b4, c[4]);
    for (i = 0; i < 4; i++) put(s, &lay->hoc[i], c[5 + i]);
    if (kind == AP_2000)
        for (i = 0; i < 40; i++) serial[i] = s[R31_TO_2400[i]];
    else
        memcpy(serial, s, kind == AP_4000 ? 80 : 48);
}

/* 4000 bps refinement fields (they stay out of the prediction memory): interpolation (bits 38-39), second layer
 * gain (40-42) and VQ (43-49), split VQ 67-70, 71-74, 75-78 and bit 66 */
static const ap_field R_INTERP = {2, {38, 39}}, R_L2G = {3, {40, 41, 42}}, R_L2V = {7, {43, 44, 45, 46, 47, 48, 49}};
static const ap_field R_L2X[4] = {{4, {67, 68, 69, 70}}, {4, {71, 72, 73, 74}}, {4, {75, 76, 77, 78}}, {1, {66}}};

/* the codes nearest to no change: no interpolation shift, gain terms near 0, the smallest vectors */
void ap_refine_neutral(int *r) {
    r[0] = 2;
    r[1] = 4;
    r[2] = 108;
    r[3] = 8;
    r[4] = 5;
    r[5] = 14;
    r[6] = 0;
}

/* envelope x (L harmonics, extended with its last value) at the 1-based harmonic positions ratio * (1..n)
 * (b25_interp_envelope) */
static void resample(const q_state *x, real ratio, int n, real *out) {
    real ext[62];
    int i;
    ext[0] = x->log2m[0];
    for (i = 1; i < 62; i++) ext[i] = x->log2m[(i <= x->L ? i : x->L) - 1];
    for (i = 0; i < n; i++) {
        real p = ratio * (real)(i + 1);
        int k = (int)p;
        real d = p - (real)k;
        if (k > 59) k = 59, d = 0;
        out[i] = (1 - d) * ext[k] + d * ext[k + 1];
    }
}

/* first-half-frame pitch of the engine (b25_merge_voiced_amplitude): the geometric mean of the two frames' when both
 * are voiced, else the voiced one's, else the current one's */
real ap_half_f0(real f0_prev, int voiced_prev, real f0_cur, int voiced_cur) {
    if (!voiced_prev) return f0_cur;
    if (!voiced_cur) return f0_prev;
    return R_SQRT(f0_prev * f0_cur);
}

int ap_half_harmonics(real f0) {
    int L = (int)((real)0.4627 / f0);
    return L > 56 ? 56 : L < 9 ? 9 : L;
}

/* 4000 bps refinements. Of the refinement fields only the split VQ (bits 66-78) is chosen by analysis by synthesis:
 * on the AMBE-3000R that lowers the distance to its own encoder's output, while choosing the interpolation and
 * second-layer fields by the same means (with ambe2-c's fitted model of them) does not; those keep the neutral codes.
 * Decoder model (decoder/src/ambeplus.c): the first half-frame envelope is the mean of the previous and the current
 * envelope resampled onto the half-frame pitch (fs, Ls harmonics), plus the neutral refinement terms; the split VQ
 * adds DCT coefficients 1..16 over the Ls harmonics. th = the first half-frame target on those harmonics. */
void ap_refine(const q_state *prev, real f0_prev, const q_state *cur, real f0_cur, real fs, const real *th, int *r) {
    const int Ls = ap_half_harmonics(fs), Lm = Ls < cur->L ? Ls : cur->L;
    real cosb[LMAX][17], res[LMAX], ps[LMAX], add[16][LMAX];
    int l, k, q;
    ap_refine_neutral(r);
    for (l = 0; l < Ls; l++)
        for (k = 0; k < 17; k++) cosb[l][k] = R_COS((real)3.141592653589793 * (real)k * ((real)l + (real)0.5) / (real)Ls);
    resample(cur, fs / f0_cur, Ls, res);
    resample(prev, fs / f0_prev, Ls, ps);
    for (l = 0; l < Ls; l++) {
        real b = (real)0.5 * (res[l] + ps[l]), d = 0;
        b += (l < Lm ? T_AP4_INTERP[r[0]][0] * (cur->log2m[l] - b) + T_AP4_INTERP[r[0]][1] : 0) + T_AP4_L2_GAIN[r[1]];
        for (k = 1; k <= 10; k++) d += T_AP4_L2_VQ[r[2]][k - 1] * cosb[l][k];
        res[l] = th[l] - b - (real)T_AP4_L2_SCALE[0] * d;
    }
    for (q = 0; q < 4; q++) { /* the fields in turn on the remaining error */
        int n = q < 3 ? 16 : 2, j, bj = 0;
        real be = (real)1e30;
        for (j = 0; j < n; j++) {
            const real *X = T_AP4_L2X_VQ[16 * q + j];
            real e = 0;
            for (l = 0; l < Ls; l++) {
                real d = 0;
                for (k = 1; k <= 16; k++) d += X[k - 1] * cosb[l][k];
                add[j][l] = d;
                e += (res[l] - d) * (res[l] - d);
            }
            if (e < be) be = e, bj = j;
        }
        for (l = 0; l < Ls; l++) res[l] -= add[bj][l];
        r[3 + q] = bj;
    }
}

void ap_pack_refine(const int *r, uint8_t *serial) {
    int q;
    put(serial, &R_INTERP, r[0]);
    put(serial, &R_L2G, r[1]);
    put(serial, &R_L2V, r[2]);
    for (q = 0; q < 4; q++) put(serial, &R_L2X[q], r[3 + q]);
}
