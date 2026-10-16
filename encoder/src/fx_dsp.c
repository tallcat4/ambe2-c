/* Fixed-point DSP: windowed 512-point power spectrum (as log2(P + 1), Q8), integer log2 / exp2. */
#include "fx_internal.h"

#define H (NFFT / 2)

static int clz64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return v ? __builtin_clzll(v) : 64;
#else
    int n = 0;
    if (!v) return 64;
    if (!(v >> 32)) n += 32, v <<= 32;
    if (!(v >> 48)) n += 16, v <<= 16;
    if (!(v >> 56)) n += 8, v <<= 8;
    if (!(v >> 60)) n += 4, v <<= 4;
    if (!(v >> 62)) n += 2, v <<= 2;
    if (!(v >> 63)) n += 1;
    return n;
#endif
}

/* log2(v) for v >= 1 in Q16: exponent + table of log2(1 + i/256) with linear interpolation (error < 1e-5) */
int32_t fx_log2_u64(uint64_t v) {
    int n = 63 - clz64(v), i;
    uint32_t f16 = n >= 16 ? (uint32_t)(v >> (n - 16)) & 0xffffu : (uint32_t)(v << (16 - n)) & 0xffffu;
    i = (int)(f16 >> 8);
    return (int32_t)(n << 16) + FX_LOG2_TAB[i] + (((FX_LOG2_TAB[i + 1] - FX_LOG2_TAB[i]) * (int32_t)(f16 & 0xff)) >> 8);
}

/* log2(v * 2^e + 1), Q16: both terms aligned to a common exponent in 64 bits */
int32_t fx_log2p1(uint64_t v, int e) {
    int ea, X, sa;
    uint64_t A, B;
    if (!v) return 0;
    ea = e + 63 - clz64(v);
    X = ea > 0 ? ea : 0;
    sa = 62 - X + e;
    A = sa >= 0 ? v << sa : (-sa >= 64 ? 0 : v >> -sa);
    B = X <= 62 ? (uint64_t)1 << (62 - X) : 0;
    return fx_log2_u64(A + B) + (int32_t)(X - 62) * 65536;
}

/* linear power from log2(P + 1) in Q8 (0 .. 46 * 256): P = 2^(lg / 256) - 1, in Q4 (at most 2^50) */
uint64_t fx_pow(int32_t lg) {
    int ip;
    uint64_t m, v;
    if (lg <= 0) return 0;
    ip = (lg >> 8) + 4;
    m = (uint64_t)FX_EXP2_TAB[lg & 255];   /* Q29 */
    v = ip >= 29 ? m << (ip - 29) : (m + ((uint64_t)1 << (28 - ip))) >> (29 - ip);
    return v - 16;
}

/* count leading zeros: the CPU's instruction where it has one, else two compares and a byte table */
#if defined(__ARM_FEATURE_CLZ) || defined(__riscv_zbb) || defined(__XTENSA__) || defined(__x86_64__) || \
    defined(__aarch64__)
static int clz32(uint32_t v) { return v ? __builtin_clz(v) : 32; }
#else
static const uint8_t CLZ8[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};   /* rest 0 */
static int clz32(uint32_t v) {
    int n = 0;
    if (v < 0x10000u) n = 16, v <<= 16;
    if (v < 0x1000000u) n += 8, v <<= 8;
    return n + CLZ8[v >> 24];
}
#endif

/* log2(xr^2 + xi^2) * 2^e + 1), Q8, in 32-bit arithmetic: xr, xi are first shifted right by s so that both fit in
 * 15 bits (squares and their sum < 2^31); the +1 becomes 2^(-e - 2s) (dropped when below one unit) */
static int16_t log2_power(int32_t xr, int32_t xi, int e) {
    uint32_t ar = (uint32_t)(xr < 0 ? -xr : xr), ai = (uint32_t)(xi < 0 ? -xi : xi), v;
    int s = 17 - clz32(ar | ai), n, i;
    uint32_t f;
    if (s < 0) s = 0;
    ar >>= s, ai >>= s;
    v = ar * ar + ai * ai + (-e - 2 * s >= 0 ? (uint32_t)1 << (-e - 2 * s) : 0);
    n = 31 - clz32(v);
    f = n >= 16 ? (v >> (n - 16)) & 0xffff : (v << (16 - n)) & 0xffff;   /* mantissa, 16 bits */
    i = (int)(f >> 8);
    return (int16_t)((((n + 2 * s + e) << 16) + FX_LOG2_TAB[i] +
                      (((FX_LOG2_TAB[i + 1] - FX_LOG2_TAB[i]) * (int32_t)(f & 0xff)) >> 8) + 128) >> 8);
}


/* lg[0..256] = log2(|X_k|^2 + 1) in Q8, X = rfft(x * w zero-padded to 512).
 * x * w (w Q14, |w| < 1.15) is scaled to at most 21 bits: (x * w) >> (9 - s), where s >= 0 is the input's headroom
 * (|x| < 2^(15 - s)). No butterfly can then exceed 2^29.2, so no per-stage scaling is needed, and quiet input keeps
 * its precision. One 256-point complex FFT of the even / odd samples packed as re / im (radix 2, twiddles Q15 applied
 * with fx_mulq16; the multiplications by 1 and -i are skipped), then the real-FFT split. */
void fx_fft_lg(const int16_t *x, const int16_t *w, int n, int16_t *lg) {
    int32_t re[H], im[H], mx = 0;
    int i, len, s = 0, sh, e;
    for (i = 0; i < n; i++) {
        int32_t v = x[i] < 0 ? -x[i] : x[i];
        if (v > mx) mx = v;
    }
    while (s < 9 && (mx << (s + 1)) < 32768) s++;
    sh = 9 - s;
    for (i = 0; i < H; i++) {
        int j = FX_BITREV[i];
        int32_t r = sh ? 1 << (sh - 1) : 0;
        re[j] = 2 * i < n ? ((int32_t)x[2 * i] * w[2 * i] + r) >> sh : 0;
        im[j] = 2 * i + 1 < n ? ((int32_t)x[2 * i + 1] * w[2 * i + 1] + r) >> sh : 0;
    }
    for (i = 0; i < H; i += 4) {   /* stages 1 and 2 (twiddles 1 and -i): one radix-4 pass without products */
        int32_t ar = re[i] + re[i + 1], ai = im[i] + im[i + 1], br = re[i] - re[i + 1], bi = im[i] - im[i + 1];
        int32_t cr = re[i + 2] + re[i + 3], ci = im[i + 2] + im[i + 3], dr = re[i + 2] - re[i + 3], di = im[i + 2] - im[i + 3];
        re[i] = ar + cr, im[i] = ai + ci, re[i + 2] = ar - cr, im[i + 2] = ai - ci;
        re[i + 1] = br + di, im[i + 1] = bi - dr, re[i + 3] = br - di, im[i + 3] = bi + dr;   /* d * -i = (di, -dr) */
    }
    for (len = 8; len <= H; len <<= 1) {
        int half = len / 2, step = NFFT / len, start, k;
        for (start = 0; start < H; start += len) {   /* k = 0: w = 1 */
            int32_t *ra = re + start, *ia = im + start, tr = ra[half], ti = ia[half];
            ra[half] = ra[0] - tr, ia[half] = ia[0] - ti, ra[0] += tr, ia[0] += ti;
        }
        for (start = half / 2; start < H; start += len) {   /* k = half / 2: w = -i */
            int32_t *ra = re + start, *ia = im + start, tr = ia[half], ti = -ra[half];
            ra[half] = ra[0] - tr, ia[half] = ia[0] - ti, ra[0] += tr, ia[0] += ti;
        }
        for (k = 1; k < half; k++) {
            int32_t c = FX_TW_COS[k * step], sn = -FX_TW_SIN[k * step];
            if (2 * k == half) continue;
            for (start = k; start < H; start += len) {
                int32_t *ra = re + start, *ia = im + start, xr = ra[half], xi = ia[half];
                int32_t tr = (fx_mulq16(xr, c) - fx_mulq16(xi, sn)) * 2, ti = (fx_mulq16(xr, sn) + fx_mulq16(xi, c)) * 2;
                ra[half] = ra[0] - tr, ia[half] = ia[0] - ti, ra[0] += tr, ia[0] += ti;
            }
        }
    }
    FX_PROF(FXP_FFT);
    /* 2 X[k] = (Z[k] + conj Z[H-k]) + e^{-2 pi i k / 512} (Z[k] - conj Z[H-k]) / i ; P = |X|^2 = P2 / 4,
     * and the true spectrum is 2^(sh - 14) times X: P_true = P2 * 2^e, e = 2 sh - 30 in [-30, -12]:
     * log2(P_true + 1) = log2(P2 + 2^-e) + e */
    e = 2 * sh - 30;
    for (i = 0; i <= H; i++) {
        int a = i % H, b = (H - i) % H;
        int32_t er = re[a] + re[b], ei = im[a] - im[b], orr = im[a] + im[b], oi = re[b] - re[a];
        int32_t xr, xi;
        if (i == 0) xr = er + orr, xi = ei + oi;
        else if (i == H) xr = er - orr, xi = ei - oi;
        else {
            int32_t c = FX_TW_COS[i], sn = -FX_TW_SIN[i];
            xr = er + (fx_mulq16(orr, c) - fx_mulq16(oi, sn)) * 2;
            xi = ei + (fx_mulq16(orr, sn) + fx_mulq16(oi, c)) * 2;
        }
        lg[i] = log2_power(xr, xi, e);
    }
    FX_PROF(FXP_LOG);
}
