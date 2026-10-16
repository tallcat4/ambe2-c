/* Power spectrum of a real block zero-padded to 512 points: one 256-point complex FFT of the even/odd samples
 * packed as re/im, then the usual split into the 257 real-FFT bins. */
#include <math.h>
#include <string.h>

#include "enc_internal.h"

#define H (NFFT / 2)

void fft_power(const real *x, int n, real *P) {
    real re[H], im[H];
    int i, j, len;
    for (i = 0; i < H; i++) {
        re[i] = 2 * i < n ? x[2 * i] : 0;
        im[i] = 2 * i + 1 < n ? x[2 * i + 1] : 0;
    }
    for (i = 1, j = 0; i < H; i++) {   /* bit reversal */
        int bit = H >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            real t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= H; len <<= 1) {   /* twiddles of the 256-point FFT = every other 512-point twiddle */
        int step = NFFT / len, start, k;
        for (start = 0; start < H; start += len)
            for (k = 0; k < len / 2; k++) {
                real c = T_TW_COS[k * step], s = -T_TW_SIN[k * step];
                int a = start + k, b = a + len / 2;
                real tr = re[b] * c - im[b] * s, ti = re[b] * s + im[b] * c;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    }
    /* X[k] = (Z[k] + conj Z[H-k]) / 2 + e^{-2 pi i k / 512} (Z[k] - conj Z[H-k]) / (2i) */
    for (i = 0; i <= H; i++) {
        int a = i % H, b = (H - i) % H;
        real er = (re[a] + re[b]) / 2, ei = (im[a] - im[b]) / 2;     /* even part */
        real orr = (im[a] + im[b]) / 2, oi = (re[b] - re[a]) / 2;    /* odd part */
        real c = i < H ? T_TW_COS[i] : -1, s = i < H ? -T_TW_SIN[i] : 0;
        real xr = er + orr * c - oi * s, xi = ei + orr * s + oi * c;
        P[i] = xr * xr + xi * xi;
    }
}

#ifdef AMBE_ENC_FLOAT
/* log2 of a positive normal float: exponent from the IEEE bits, log2 of the mantissa m in
 * [1, 2) as 2/ln2 * atanh((m - 1) / (m + 1)), series to t^9 (|t| < 0.172, error < 2e-8, below float resolution) */
real fast_log2(real x) {
    union { float f; uint32_t u; } v;
    float m, t, t2;
    int e;
    v.f = x;
    e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;   /* mantissa in [1, 2) */
    m = v.f;
    if (m > 1.41421356f) m *= 0.5f, e += 1;      /* m in [0.707, 1.414): |t| < 0.172 */
    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    return 2.88539008f * t * (1.0f + t2 * (1.0f / 3 + t2 * (1.0f / 5 + t2 * (1.0f / 7 + t2 * (1.0f / 9))))) + (float)e;
}
#else
/* log2 via exponent/mantissa split and the atanh series on [1, 2) (max error ~1e-7) */
real fast_log2(real x) {
    int e;
    double m = frexp((double)x, &e) * 2.0;   /* x = m * 2^(e - 1), m in [1, 2) */
    double t = (m - 1.0) / (m + 1.0), t2 = t * t;
    /* log2(m) = 2/ln2 * atanh(t), series to t^13 */
    double s = t * (1.0 + t2 * (1.0 / 3 + t2 * (1.0 / 5 + t2 * (1.0 / 7 + t2 * (1.0 / 9 + t2 * (1.0 / 11 + t2 / 13))))));
    return (real)(2.8853900817779268 * s + (double)(e - 1));
}
#endif
