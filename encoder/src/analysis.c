/* Per-frame analysis: shared spectra, pitch candidates, voicing features, harmonic amplitudes.
 * Mirrors lite/lite_encoder.py (spectra, comb_features, pitch_scores, voicing_frame, harmonic_log2_amplitudes). */
#include <math.h>
#include <string.h>

#include "enc_internal.h"

static const int SHIFTS[5] = {0, -45, 45, -26, 26};
#define AMP_CENTER (-208)

static real mean_sq(const real *x, int n) {
    real s = 0;
    int i;
    for (i = 0; i < n; i++) s += x[i] * x[i];
    return s / n;
}

static void windowed_power(const real *x, const real *w, int n, real *P) {
    real seg[NFFT];
    int i;
    for (i = 0; i < n; i++) seg[i] = x[i] * w[i];
    fft_power(seg, n, P);
}

/* the amplitude spectrum (window T_AMP_WINDOW) centred `center` samples from the frame start x */
void amp_spectrum(const real *x, int center, real *P) { windowed_power(x + center - 200, T_AMP_WINDOW, 400, P); }

/* comb features of one spectrum for every candidate: energy share on the harmonic comb, comb log rise */
static void comb(const real *P, const real *lg, int k, real C[NCAND][NCF]) {
    real sumP = 0, meanL = 0;
    int i, b;
    for (i = 0; i < NB; i++) {
        sumP += P[i];
        meanL += lg[i];
    }
    meanL /= NB;
    for (b = 0; b < NCAND; b++) {
        real f0 = T_W0[b], step = f0 * NFFT, sp = 0, sl = 0;
        int l, n = 0;
        for (l = 1; l <= 60 && (real)l * f0 < (real)0.475; l++) {
            real pos = (real)l * step;   /* = l * f0 * NFFT */
            int i0 = (int)pos, i1 = i0 + 1 < NB - 1 ? i0 + 1 : NB - 1;
            real fr = pos - (real)i0;
            sp += P[i0] * (1 - fr) + P[i1] * fr;
            sl += lg[i0] * (1 - fr) + lg[i1] * fr;
            n++;
        }
        C[b][2 * k] = sp / (sumP + (real)1e-3);
        C[b][2 * k + 1] = sl / n - meanL;
    }
}

static void pitch_logits(frame_an *a) {
    real x[41], lin[NCAND];
    int b, s, k, j, used[NCAND];
    for (b = 0; b < NCAND; b++) {   /* linear prefilter on the raw features (standardisation folded in) */
        real d = T_P_LINB_F + (a->lv / 10) * T_P_LINW_F[40];
        for (s = 0; s < 5; s++) {
            int jj = b + SHIFTS[s];
            jj = jj < 0 ? 0 : jj > NCAND - 1 ? NCAND - 1 : jj;
            for (k = 0; k < NCF; k++) d += a->C[jj][k] * T_P_LINW_F[s * NCF + k];
        }
        lin[b] = d + T_P_LIN_BIAS[b] + a->lv * T_P_LIN_LEVEL_BIAS[b] / 10;
        a->logits[b] = (real)-1e4;
        used[b] = 0;
    }
    for (k = 0; k < TOPK; k++) {   /* MLP on the prefilter's best TOPK (ties: lower index first) */
        int best = -1;
        real h, y = 0;
        for (b = 0; b < NCAND; b++)
            if (!used[b] && (best < 0 || lin[b] > lin[best])) best = b;
        used[best] = 1;
        for (s = 0; s < 5; s++) {
            int jj = best + SHIFTS[s];
            jj = jj < 0 ? 0 : jj > NCAND - 1 ? NCAND - 1 : jj;
            for (j = 0; j < NCF; j++) x[s * NCF + j] = a->C[jj][j];
        }
        x[40] = a->lv / 10;
        for (j = 0; j < T_P_HIDDEN; j++) {
            h = T_P_B1_F[j];
            for (s = 0; s < 41; s++) h += T_P_W1_F[j][s] * x[s];
            if (h > 0) y += T_P_W2[j] * h;
        }
        a->logits[best] = y + T_P_B2 + (T_P_BIAS[best] + a->lv * T_P_LEVEL_BIAS[best] / 10);
    }
}

/* oldest / newest analysed sample of a frame, relative to its start (all spectrum windows) */
#define SPAN_LO (-472)
#define SPAN_HI (-8)

void analyse_frame(const real *x, frame_an *a, real *acc, int first) {
    real lv220;
    int k;
    a->silent = 1;   /* the AMBE-3000R codes a constant (DC-free: zero) input as its silence frame */
    for (k = SPAN_LO + 1; k < SPAN_HI && a->silent; k++) a->silent = x[k] == x[SPAN_LO];
    windowed_power(x + T_SPEC_CENTER[0] - 200, T_AMP_WINDOW, 400, a->S[0]);
    windowed_power(x + T_SPEC_CENTER[1] - 100, T_HANN200, 200, a->S[1]);
    windowed_power(x + T_SPEC_CENTER[2] - 128, T_HANN256, 256, a->S[2]);
    windowed_power(x + T_SPEC_CENTER[3] - 192, T_HANN384, 384, a->S[3]);
    for (k = 0; k < NSPEC; k++) {
        int m;
        for (m = 0; m < NB; m++) a->lg[k][m] = fast_log2(a->S[k][m] + 1);
        comb(a->S[k], a->lg[k], k, a->C);
    }
    a->lv = (real)0.5 * fast_log2(mean_sq(x + T_LEVEL_CENTER - T_LEVEL_LEN / 2, T_LEVEL_LEN) + 1);
    lv220 = (real)0.5 * fast_log2(mean_sq(x + AMP_CENTER - 110, 220) + 1);
    if (first) acc[0] = acc[1] = acc[2] = lv220;
    for (k = 0; k < 3; k++) acc[k] = T_LEVEL_DECAYS[k] * acc[k] + (1 - T_LEVEL_DECAYS[k]) * lv220;
    a->lvl[0] = lv220;
    a->lvl[1] = acc[0];
    a->lvl[2] = acc[1];
    a->lvl[3] = acc[2];
    pitch_logits(a);
}

/* ---- voicing */
void band_of_harmonics(real f0, int L, int *band) {
    real bk[9];
    int k, l;
    for (k = 0; k < 9; k++) bk[k] = R_FLOOR(((real)k - (real)0.25) / (16 * f0));
    for (l = 1; l <= L; l++) {
        int c = 0;
        for (k = 0; k < 9; k++) c += (real)l > bk[k];
        c -= 1;
        band[l - 1] = c < 0 ? 0 : c > 7 ? 7 : c;
    }
}

static real interp(const real *x, real pos) {
    int i0;
    real fr;
    if (pos < 0) pos = 0;
    if (pos > (real)(NB - 1.001)) pos = (real)(NB - 1.001);
    i0 = (int)pos;   /* pos >= 0: truncation = floor */
    fr = pos - (real)i0;
    return x[i0] * (1 - fr) + x[i0 + 1] * fr;
}

/* round half to even (numpy rint), for 0 <= v < 2^31 */
static real rint_even(real v) {
    long i = (long)v;
    real f = (real)i, d = v - f;
    if (d > (real)0.5) return f + 1;
    if (d < (real)0.5) return f;
    return (i & 1) ? f + 1 : f;
}

/* sum of the squared amplitude window (computed once; the same summation as per frame before) */
static real amp_window_energy(void) {
    static real w2 = 0;   /* a race between encoders only computes the same value twice */
    if (w2 == 0) {
        real s = 0;
        int m;
        for (m = 0; m < 400; m++) s += T_AMP_WINDOW[m] * T_AMP_WINDOW[m];
        w2 = s;
    }
    return w2;
}

void voicing_frame(const frame_an *a, int b0, real *x, real *raw) {
    real f0 = T_W0[b0], cnt[8] = {0}, csum[3][8] = {{0}}, be[8] = {0}, bem = 0, win2 = 0, sum0 = 0;
    real tot[2][8], nr[2][8], lmax;
    int L = T_L[b0], band[LMAX], l, s, k, m, n = 0;
    band_of_harmonics(f0, L, band);
    const real(*lg)[NB] = a->lg;
    for (l = 1; l <= L; l++) cnt[band[l - 1]] += 1;
    for (s = 0; s < 3; s++)
        for (l = 1; l <= L; l++) {
            real pk = interp(lg[s], (real)l * f0 * NFFT);
            real va = (real)0.5 * (interp(lg[s], ((real)l - (real)0.5) * f0 * NFFT) + interp(lg[s], ((real)l + (real)0.5) * f0 * NFFT));
            csum[s][band[l - 1]] += pk - va;
        }
    for (s = 0; s < 3; s++)
        for (k = 0; k < 8; k++) x[n++] = csum[s][k] / (cnt[k] + (real)1e-9);
    for (s = 0; s < 2; s++)
        for (k = 0; k < 8; k++) tot[s][k] = 0, nr[s][k] = 0;
    for (m = 0; m < NB; m++) {   /* near-harmonic energy share per band, 200- and 256-sample spectra */
        real h = rint_even((real)m / (f0 * NFFT));
        int hi = (int)h;
        if (h < 1 || h > L) continue;
        k = band[hi - 1];
        for (s = 0; s < 2; s++) tot[s][k] += a->S[1 + s][m];
        if (R_FABS((real)m - h * f0 * NFFT) <= (real)1.5)
            for (s = 0; s < 2; s++) nr[s][k] += a->S[1 + s][m];
    }
    for (s = 0; s < 2; s++)
        for (k = 0; k < 8; k++) x[n++] = nr[s][k] / (tot[s][k] + 1);
    for (l = 1; l <= L; l++) be[band[l - 1]] += interp(a->S[0], (real)l * f0 * NFFT);
    for (k = 0; k < 8; k++) {
        be[k] = fast_log2(be[k] + 1);
        bem += be[k];
    }
    bem /= 8;
    for (k = 0; k < 8; k++) x[n++] = be[k] - bem;
    for (m = 0; m < NB; m++) sum0 += a->S[0][m];
    win2 = amp_window_energy();
    x[n++] = (real)0.5 * fast_log2(sum0 / win2 + 1);
    for (k = 0; k < 4; k++) x[n++] = a->lvl[k];
    for (lmax = a->logits[0], k = 1; k < NCAND; k++)
        if (a->logits[k] > lmax) lmax = a->logits[k];
    x[n++] = lmax - a->logits[b0];
    x[n++] = fast_log2(f0);
    x[n++] = (real)L / 56;
    for (k = 0; k < NCF; k++) x[n++] = a->C[b0][k];
    raw[0] = x[24 + 16 + 8];
    for (k = 0; k < 8; k++) raw[1 + k] = be[k];
}

int voicing_classify(const real rows[4][VF]) {
    real xin[4 * VF], h1[64], h2[64];
    int i, j, best = 0;
    real bv = 0;
    for (i = 0; i < 4; i++)
        for (j = 0; j < VF; j++) xin[i * VF + j] = rows[i][j];   /* standardisation folded into T_V_W1_F */
    for (j = 0; j < T_V_HIDDEN; j++) {
        real h = T_V_B1_F[j];
        for (i = 0; i < T_V_IN; i++) h += T_V_W1_F[j][i] * xin[i];
        h1[j] = h > 0 ? h : 0;
    }
    for (j = 0; j < T_V_HIDDEN; j++) {
        real h = T_V_B2[j];
        for (i = 0; i < T_V_HIDDEN; i++) h += T_V_W2[j][i] * h1[i];
        h2[j] = h > 0 ? h : 0;
    }
    for (j = 0; j < 32; j++) {
        real o = T_V_B3[j];
        for (i = 0; i < T_V_HIDDEN; i++) o += T_V_W3[j][i] * h2[i];
        if (j == 0 || o > bv) { bv = o; best = j; }
    }
    return best;
}

/* ---- harmonic amplitudes (soft band edges evaluated only within +-15 widths of each edge) */
void harmonic_amplitudes(const real *P, real w0, int L, const real *v, real *y) {
    real bw = T_AMP_BW, lw = fast_log2(w0);
    int l, m, nt = 64;
    for (l = 1; l <= L; l++) {
        real En = 0, xt, fr, tv, tu;
        int i0;
        /* only bins within the harmonic's band (+-15 edge widths) can contribute; +-1 bin margin */
        real k0 = w0 * NFFT / (2 * (real)3.141592653589793), half = (real)0.5 + 15 * bw;
        int lo = (int)(((real)l - half) * k0) - 1, hi = (int)(((real)l + half) * k0) + 2;
        if (lo < 0) lo = 0;
        if (hi > NB) hi = NB;
        for (m = lo; m < hi; m++) {
            real d = R_FABS((real)m * 2 * (real)3.141592653589793 / NFFT / w0 - (real)l);
            if (d < (real)0.5 - 15 * bw) En += P[m];
            else if (d < (real)0.5 + 15 * bw) En += P[m] / (1 + R_EXP(-((real)0.5 - d) / bw));
        }
        xt = w0 * (real)l / (real)3.141592653589793;
        xt = (xt < 0 ? 0 : xt > 1 ? 1 : xt) * (nt - 1);
        i0 = (int)xt;   /* xt >= 0 */
        if (i0 > nt - 2) i0 = nt - 2;
        fr = xt - (real)i0;
        tv = T_AMP_TAB_V[i0] * (1 - fr) + T_AMP_TAB_V[i0 + 1] * fr;
        tu = T_AMP_TAB_U[i0] * (1 - fr) + T_AMP_TAB_U[i0 + 1] * fr;
        y[l - 1] = (real)0.5 * fast_log2(En + 1) + v[l - 1] * (T_AMP_OFF_V + T_AMP_POW_V * lw + tv) +
                   (1 - v[l - 1]) * (T_AMP_OFF_U + T_AMP_POW_U * lw + tu);
    }
}
