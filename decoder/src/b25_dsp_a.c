/* C port of blip25-vocoder src/engine/dsp.rs lines 1-2207 (part dsp_a). */
#include "b25_internal.h"

/* ======================= from prim ======================= */

/* 0x1030b2b0 - advance a 16-bit LCG in place */
void b25_rand_step(int16_t *seed)
{
    *seed = w16_add(w16_mul(*seed, (int16_t)(uint16_t)0xADu), 0x3619);
}

/* 0x1030b620 - fill n 16-bit words with val */
size_t b25_fill16(int16_t *dst, int16_t val, int16_t n)
{
    size_t nn, i;
    if (n <= 0) {
        return 0;
    }
    nn = (size_t)n;
    for (i = 0; i < nn; i++) {
        dst[i] = val;
    }
    return nn;
}

/* 0x10310220 - one conditional-subtract division step (SUBC) */
void b25_subc(int16_t divisor, int32_t *acc)
{
    int32_t diff = w32_sub(*acc, w32_shl((int32_t)divisor, 15));
    *acc = diff < 0 ? w32_mul(*acc, 2) : w32_add(w32_mul(diff, 2), 1);
}

/* ======================= from batch1 ======================= */

/* norm (leading-redundant-sign-bit count) with degenerate cases */
int32_t b25_norm_shift_batch1(uint32_t x)
{
    uint32_t mag = ((int32_t)x < 0) ? ~x : x;
    return 0x1e - b25_top_bit_batch3(mag);
}

/* exponent-align shift */
int32_t b25_shift_i32_batch1(int32_t v, int16_t sh)
{
    if (sh < 0) {
        if (sh < -0x1e) {
            return v >> 31;
        } else {
            return v >> ((uint32_t)w32_neg((int32_t)sh) & 0x1f);
        }
    } else {
        return w32_shl(v, (uint32_t)(int32_t)sh & 0x1f);
    }
}

/* saturating 32-bit subtract */
int32_t b25_sat_sub_batch1(int32_t a, int32_t b)
{
    int32_t d = w32_sub(a, b);
    if (((~b ^ d) & (d ^ a)) < 0) {
        if (a < 0) {
            return INT32_MIN;
        } else {
            return INT32_MAX;
        }
    } else {
        return d;
    }
}

/* 0x10320530 - MSVC __allshr */
int64_t b25_allshr(int64_t v, uint8_t count)
{
    int32_t hi = (int32_t)(v >> 32);
    uint32_t lo = (uint32_t)v;
    int32_t fill = hi >> 31;
    uint32_t new_lo;
    int32_t new_hi;
    int32_t new_lo_s;
    if (count >= 0x40) {
        return (int64_t)(((uint64_t)(uint32_t)fill << 32) | (uint64_t)(uint32_t)fill);
    }
    if (count < 0x20) {
        uint8_t n = (uint8_t)(count & 0x1f);
        if (n == 0) {
            new_lo = lo;
        } else {
            new_lo = (lo >> n) | ((uint32_t)hi << (32 - n));
        }
        new_hi = hi >> n;
        return (int64_t)(((uint64_t)(uint32_t)new_hi << 32) | (uint64_t)new_lo);
    }
    new_lo_s = hi >> (count & 0x1f);
    return (int64_t)(((uint64_t)(uint32_t)fill << 32) | (uint64_t)(uint32_t)new_lo_s);
}

/* 0x1031ecc0 - population count over the low n bits */
int32_t b25_popcount_low(uint32_t x, uint16_t n)
{
    int32_t acc = 0;
    if ((int16_t)n > 0) {
        int32_t xx = (int32_t)x;
        uint16_t k;
        for (k = 0; k < n; k++) {
            acc += xx & 1;
            xx >>= 1;
        }
    }
    return acc;
}

/* 0x1030cdb0 - classify a channel-unit length into one of five frame classes */
int8_t b25_frame_size_class(int16_t x)
{
    uint16_t u = (uint16_t)x;
    if (w16u_sub(u, 5) < 0x76) {
        return 0;
    }
    if (w16u_sub(u, 0x80) < 0x10) {
        return 1;
    }
    if (w16u_sub(u, 0x90) < 0x10) {
        return 2;
    }
    return (int8_t)(3 + (int8_t)(w16u_sub(u, 0xa0) > 3));
}

/* 0x10316350 - sum n words, normalise, return block exponent */
int32_t b25_sum_norm(int16_t *out, const int16_t *v, int32_t exp, uint16_t n)
{
    uint32_t sum = 0;
    uint32_t sh;
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            sum = sum + (uint32_t)(int32_t)v[i];
        }
    }
    sh = (sum == 0) ? 0u : ((uint32_t)b25_norm_shift_batch1(sum) & 0xffff);
    *out = (int16_t)((sum << (sh & 0x1f)) >> 16);
    return w32_add(w32_sub(exp, (int32_t)sh), 0x10);
}

/* 0x10312570 - expand 32 packed dibits into two words of doubled dibits */
void b25_expand_dibits(uint32_t *out0, uint32_t *out1, uint32_t bits)
{
    int32_t src = (int32_t)bits;
    int w;
    for (w = 0; w < 2; w++) {
        uint32_t acc = 0;
        int32_t b = 30;
        int k;
        for (k = 0; k < 8; k++) {
            uint32_t d = (uint32_t)(src & 3);
            src >>= 2;
            acc |= (d << ((uint32_t)b & 31)) | (d << ((uint32_t)(b - 2) & 31));
            b -= 4;
        }
        if (w == 0) {
            *out0 = acc;
        } else {
            *out1 = acc;
        }
    }
}

/* 0x10313e60 - pick a pitch-refinement offset from a pair of voicing masks */
void b25_voiced_offset(int16_t *out, uint32_t mask_a, int16_t a, uint32_t mask_b, int16_t b)
{
    if ((mask_a & 0xaaaaaaaau) == 0) {
        *out = 0;
        return;
    }
    if (a > 0 && (mask_b & 0xaaaaaaaau) != 0) {
        *out = w16_sub(a, w16_mul(b, 256));
        return;
    }
    *out = w16_mul((int16_t)(int8_t)b, 128);
}

/* 0x10317520 - block exponent of a 16-bit vector from its peak magnitude */
int32_t b25_norm_exp_absmax(const int16_t *v, uint16_t n)
{
    int16_t m = 0;
    int32_t x;
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int16_t xv = v[i];
            int16_t a;
            if (xv < 0) {
                if (xv == INT16_MIN) {
                    a = 0x7fff;
                } else {
                    a = (int16_t)(-xv);
                }
            } else {
                a = xv;
            }
            if (m < a) {
                m = a;
            }
        }
    }
    x = w32_shl((int32_t)m, 16);
    if (x != 0) {
        return -(int32_t)((uint32_t)b25_norm_shift_batch1((uint32_t)x) & 0xffff);
    } else {
        return 0;
    }
}

/* 0x10319f80 - block exponent of the largest tagged value in [from, to) */
int32_t b25_norm_exp_max_tagged(const int16_t *vals, const int16_t *tags, int16_t from, int16_t to, int16_t tag)
{
    uint32_t best = 0;
    if (from < to) {
        size_t count = (size_t)(uint16_t)w16_sub(to, from);
        size_t i;
        for (i = (size_t)from; i < (size_t)from + count; i++) {
            uint32_t scaled = (uint32_t)w32_shl((int32_t)vals[i], 16);
            if (tags[i] == tag && (int32_t)best < (int32_t)scaled) {
                best = scaled;
            }
        }
    }
    if (best != 0) {
        return -(int32_t)((uint32_t)b25_norm_shift_batch1(best) & 0xffff);
    } else {
        return 0;
    }
}

/* 0x103124b0 - expand a voicing-decision codebook index into a band mask */
uint32_t b25_voicing_pattern(int16_t idx, uint32_t prev, int16_t nbits)
{
    int16_t nb;
    uint16_t pat = 0;
    int16_t s = 0;
    uint32_t x;
    if (idx <= 0x37) {
        return DSPT_VOICING_LUT[(size_t)idx];
    }
    nb = nbits > 8 ? 8 : nbits;
    while (s < nb) {
        int16_t rep = DSPT_BIT_ALLOC[((size_t)nb - 3) * 8 + (size_t)s];
        if (rep > 0) {
            uint16_t bit = (uint16_t)((((int32_t)w16_sub(idx, 0x38)) >> ((uint32_t)(int32_t)s & 0x1f)) & 1);
            uint16_t k;
            for (k = 0; k < (uint16_t)rep; k++) {
                pat = (uint16_t)((uint16_t)((int16_t)pat >> 2) | (uint16_t)((uint32_t)bit << 14));
            }
        }
        s++;
    }
    x = (prev & 0x5555) ^ (uint32_t)pat;
    return ((x * 4u) & x) | (prev & 0x5555 & (uint32_t)pat) | (uint32_t)w32_shl((int32_t)(int16_t)pat, 16);
}

/* 0x1031a060 - one complex butterfly pass over a Hermitian-symmetric spectrum */
void b25_fft_butterfly_pass(int16_t *buf, size_t a_off, size_t b_off, int16_t stride, uint16_t n)
{
    ptrdiff_t pa, pb, ti;
    uint16_t k;
    if ((int16_t)n <= 0) {
        return;
    }
    pa = (ptrdiff_t)a_off;
    pb = (ptrdiff_t)b_off;
    ti = 0;
    for (k = 0; k < n; k++) {
        int32_t c, s, half, rot, br, rot2, half2;
        ti += (ptrdiff_t)stride;
        c = (int32_t)TB_COS512[(size_t)ti];
        s = (int32_t)TB_COS512[128 + (size_t)ti];

        half = w32_shl((int32_t)buf[(size_t)pa], 16) >> 1;
        rot = w32_sub(w32_mul(c, (int32_t)buf[(size_t)pb]), w32_mul(s, (int32_t)buf[(size_t)(pb + 1)]));
        buf[(size_t)pa] = (int16_t)((uint32_t)w32_add(half, rot) >> 16);
        br = (int32_t)buf[(size_t)pb];
        buf[(size_t)pb] = (int16_t)((uint32_t)w32_sub(half, rot) >> 16);

        rot2 = w32_add(w32_mul(s, br), w32_mul(c, (int32_t)buf[(size_t)(pb + 1)]));
        half2 = w32_shl((int32_t)buf[(size_t)(pa + 1)], 16) >> 1;
        buf[(size_t)(pa + 1)] = (int16_t)((uint32_t)w32_add(half2, rot2) >> 16);
        buf[(size_t)(pb + 1)] = (int16_t)((uint32_t)w32_sub(rot2, half2) >> 16);

        pa += 2;
        pb -= 2;
    }
}

/* closure `step` of pow2_frac */
static int16_t b25_dsp_a_pow2_step(int32_t acc, int32_t mul, uint32_t sh, uint32_t mask, int32_t add)
{
    return (int16_t)(w32_add((w32_add(w32_mul(acc, mul), 0x8000) >> sh) & (int32_t)mask, add) >> 16);
}

/* 0x1030a7a0 - 2^x split into an integer exponent and a Q31 mantissa */
int32_t b25_pow2_frac(int32_t x, int16_t *exp)
{
    int32_t t;
    int16_t r;
    *exp = w16_add((int16_t)(x >> 16), 1);
    t = (int32_t)(((uint32_t)x & 0xffff) >> 1);

    r = b25_dsp_a_pow2_step(t, 0xaec4, 3, 0xffffe000u, (int32_t)0x4ecb0000u);
    r = b25_dsp_a_pow2_step(w32_mul(w32_mul((int32_t)r, t), 2), 1, 3, 0xffffe000u, 0x38d60000);
    r = b25_dsp_a_pow2_step(w32_mul(w32_mul((int32_t)r, t), 2), 1, 2, 0xffffc000u, 0x3d7f8000);
    r = b25_dsp_a_pow2_step(w32_mul(w32_mul((int32_t)r, t), 2), 1, 1, 0xffff8000u, 0x58b90000);
    return w32_add((w32_add(w32_mul(w32_mul((int32_t)r, t), 2), 0x8000) >> 1) & (int32_t)0xffff8000u,
                   0x40000000);
}

/* closure `decay` of spread_peak */
static int16_t b25_dsp_a_spread_decay(int16_t cur, int16_t dec)
{
    int32_t d = b25_sat_sub_batch1(w32_shl((int32_t)cur, 16), w32_shl((int32_t)dec, 16));
    int16_t next = (int16_t)(d >> 16);
    if (next < -0x77ff) {
        return -0x77ff;
    } else {
        return next;
    }
}

/* 0x1030d1b0 - impose a decaying skirt around a spectral peak */
void b25_spread_peak(int16_t *arr, int16_t peak, int16_t level, int16_t dec)
{
    const int16_t FLOOR = -0x77ff;
    int16_t start = level < FLOOR ? FLOOR : level;
    int16_t cur;
    int16_t i;
    int16_t first;

    cur = start;
    i = (int16_t)(peak - 1);
    while (i >= 0) {
        if (arr[(size_t)i] < cur) {
            arr[(size_t)i] = cur;
        }
        cur = b25_dsp_a_spread_decay(cur, dec);
        i -= 1;
    }

    cur = start;
    first = w16_add(peak, 1);
    if (first < 0x38) {
        for (i = first; i < 0x38; i++) {
            if (arr[(size_t)i] < cur) {
                arr[(size_t)i] = cur;
            }
            cur = b25_dsp_a_spread_decay(cur, dec);
        }
    }
}

/* 0x1030b490 - (m2@e2) - (m1@e1), renormalised */
int16_t b25_sub_norm(int16_t m1, int16_t e1, int16_t m2, int16_t e2, int16_t *exp)
{
    return b25_addsub_norm(m1, e1, m2, e2, exp, 1);
}

/* body shared by add_norm and sub_norm */
int16_t b25_addsub_norm(int16_t a, int16_t a_exp, int16_t b, int16_t b_exp, int16_t *out_exp, int sub)
{
    if (a != 0 && (int32_t)b_exp - (int32_t)a_exp < 0x20) {
        int16_t m;
        uint32_t va, vb, sum;
        int16_t nrm, r;
        if (b == 0 || (int32_t)a_exp - (int32_t)b_exp > 0x1f) {
            *out_exp = a_exp;
            if (!sub) {
                return a;
            } else if (a == INT16_MIN) {
                return 0x7fff;
            } else {
                return (int16_t)(-a);
            }
        }
        m = w16_add(a_exp <= b_exp ? b_exp : a_exp, 1);
        va = (uint32_t)b25_shift32_al2b2(w32_mul((int32_t)a, 0x10000), w16_sub(a_exp, m));
        vb = (uint32_t)b25_shift32_al2b2(w32_mul((int32_t)b, 0x10000), w16_sub(b_exp, m));
        if (sub) {
            sum = vb - va;
        } else {
            sum = vb + va;
        }
        nrm = (sum == 0) ? (int16_t)0 : (int16_t)b25_norm_exp_batch3(sum);
        r = (int16_t)((sum << ((uint32_t)(int32_t)nrm & 0x1f)) >> 16);
        if (r == 0) {
            *out_exp = 0;
            return 0;
        }
        *out_exp = w16_sub(m, nrm);
        return r;
    }
    if (b == 0) {
        *out_exp = 0;
        return 0;
    }
    *out_exp = b_exp;
    return b;
}

/* 0x1031a870 - in-place bit-reversal permutation */
void b25_bit_reverse(int16_t *buf, int16_t size)
{
    const int16_t *tbl;
    ptrdiff_t pa, pb;
    size_t k, cnt;
    switch (size) {
    case 8: tbl = DSPT_BITREV8; break;
    case 16: tbl = DSPT_BITREV16; break;
    case 32: tbl = DSPT_BITREV32; break;
    case 64: tbl = DSPT_BITREV64; break;
    default: tbl = DSPT_BITREV128; break;
    }
    if (tbl[0] <= 0) {
        return;
    }
    pa = 0;
    pb = 0;
    cnt = (size_t)(uint16_t)tbl[0];
    for (k = 0; k < cnt; k++) {
        ptrdiff_t ia = pa + (ptrdiff_t)tbl[1 + 2 * k];
        ptrdiff_t ib;
        int16_t tmp;
        pa = ia + 1;
        ib = pb + (ptrdiff_t)tbl[2 + 2 * k];
        pb = ib + 1;
        tmp = buf[(size_t)ia]; buf[(size_t)ia] = buf[(size_t)ib]; buf[(size_t)ib] = tmp;
        tmp = buf[(size_t)pa]; buf[(size_t)pa] = buf[(size_t)pb]; buf[(size_t)pb] = tmp;
    }
}

/* ======================= from batch2 ======================= */

/* TI-C5x style signed shift */
uint32_t b25_shr_or_shl(int32_t x, int16_t sh)
{
    return (uint32_t)b25_shift_i32_batch1(x, sh);
}

/* 0x10317060 - gather eight dibits from a word, reversing their order */
uint32_t b25_gather_dibits(int32_t x)
{
    int32_t shift = 14;
    uint16_t out = 0;
    int k;
    for (k = 0; k < 8; k++) {
        uint16_t lo = (uint16_t)((uint16_t)x & 3);
        x >>= 4;
        out = (uint16_t)(out | (uint16_t)((uint32_t)lo << ((uint32_t)shift & 0x1f)));
        shift -= 2;
    }
    return 0xffff0000u | (uint32_t)out;
}

/* 0x10311e00 - in-place saturating vector subtract */
void b25_vsub_sat(int16_t *a, const int16_t *b, uint16_t n)
{
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t v = (int32_t)a[i] - (int32_t)b[i];
            if (v >= 0x8000) {
                v = 0x7fff;
            } else if (v < -0x7fff) {
                v = -0x7fff;
            }
            a[i] = (int16_t)v;
        }
    }
}

/* 0x10310150 - normalisation shift count of a 64-bit value {hi:lo} */
int32_t b25_norm64(uint32_t lo, uint32_t hi)
{
    uint32_t p1, p2, u2, u1;
    int32_t i;
    if (lo == 0 && hi == 0) {
        return 0;
    }
    p1 = lo;
    p2 = hi;
    if ((int32_t)p2 < 1 && (int32_t)p2 < 0) {
        p1 = ~p1;
        p2 = ~p2;
    }
    u2 = 0;
    u1 = 0x80;
    i = 0;
    for (;;) {
        if ((u2 & p1) != 0 || (u1 & p2) != 0) {
            break;
        }
        u2 = (u2 >> 1) | (u1 << 0x1f);
        i += 1;
        u1 = (uint32_t)((int32_t)u1 >> 1);
        if (!((int16_t)i < 0x28)) {
            break;
        }
    }
    return i - 9;
}

/* 0x1030b3d0 - shift to Q15-ish scale and round to a 16-bit magnitude */
uint32_t b25_shift_round16_batch2(int32_t x, int16_t sh)
{
    int16_t s = (int16_t)(sh - 0xf);
    uint32_t u = b25_shr_or_shl(x, s);
    if ((int32_t)u < 0) {
        if (u == 0x80000000u) {
            u = 0x7fffffffu;
        } else {
            u = 0u - u;
        }
        if ((u & 0xffff0000u) == 0x80000000u) {
            return 0x7fffffffu;
        }
        return 0u - (u & 0xffff0000u);
    }
    return u & 0xffff0000u;
}

/* 0x1030a5a0 - interpolated lookup into the 512-entry cosine table */
int32_t b25_cos_lookup(int16_t x, const int16_t *table)
{
    int32_t u = w32_shl((int32_t)x, 16) >> 6;
    uint32_t uu;
    size_t idx0, idx1;
    int32_t frac;
    if (u < 0) {
        if ((uint32_t)u == 0x80000000u) {
            u = 0x7fffffff;
        } else {
            u = w32_neg(u);
        }
    }
    uu = (uint32_t)u;
    idx0 = (size_t)(int16_t)(uu >> 16);
    idx1 = (size_t)((((uint32_t)(u >> 16) & 0xffff) + 1u) & 0x1ff);
    frac = (int32_t)(uu & 0xffff);
    return w32_add(w32_mul((int32_t)table[idx0], 0x10000 - frac), w32_mul((int32_t)table[idx1], frac)) >> 16;
}

/* 0x1031a140 - radix-2 butterfly, variant A */
void b25_butterfly_a(int16_t *p1, int16_t *p2, size_t p2_start, uint16_t n)
{
    b25_butterfly(p1, p2, p2_start, n, 0);
}

/* body shared by butterfly_a and butterfly_b */
void b25_butterfly(int16_t *p1, int16_t *p2, size_t p2_start, uint16_t n, int flip)
{
    if ((int16_t)n > 0) {
        size_t i1 = 0;
        size_t i2 = p2_start;
        uint16_t k;
        for (k = 0; k < n; k++) {
            int32_t s1 = (int32_t)p2[i2];
            int32_t s2 = (int32_t)p2[i2 + 1];
            int32_t s3 = (int32_t)p1[i1];
            int32_t s4 = (int32_t)p1[i1 + 1];
            int32_t d1, d2;
            if (flip) {
                d1 = s2 - s4;
                d2 = s3 - s1;
            } else {
                d1 = s4 - s2;
                d2 = s1 - s3;
            }
            p1[i1] = (int16_t)((s1 + s3) >> 1);
            p1[i1 + 1] = (int16_t)(d1 >> 1);
            p2[i2] = (int16_t)((s4 + s2) >> 1);
            p2[i2 + 1] = (int16_t)(d2 >> 1);
            i2 = i2 - 2;
            i1 += 2;
        }
    }
}

/* 0x10319ff0 - radix-2 butterfly, variant B */
void b25_butterfly_b(int16_t *p1, int16_t *p2, size_t p2_start, uint16_t n)
{
    b25_butterfly(p1, p2, p2_start, n, 1);
}

/* 0x10311a20 - copy an MBE parameter block (voiced case) or clear it */
void b25_copy_params_batch2(int16_t *dst, const int16_t *src, int16_t p3)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[3] = src[3];
    if (src[0] == 1) {
        int16_t cnt;
        uint16_t uv1;
        dst[2] = src[2];
        dst[6] = src[6];
        dst[4] = src[4];
        dst[5] = src[5];
        dst[0x45] = p3;
        dst[7] = src[7];
        cnt = src[2];
        if (cnt > 0) {
            size_t i;
            for (i = 0; i < (size_t)cnt; i++) {
                dst[8 + i] = src[8 + i];
            }
        }
        uv1 = (uint16_t)src[2];
        if ((int16_t)uv1 < 0x38) {
            int16_t fillval = src[(size_t)src[2] + 7];
            uint32_t count = (0x38u - (uint32_t)uv1) & 0xffff;
            size_t j = (size_t)uv1 + 8;
            uint32_t k;
            for (k = 0; k < count; k++) {
                dst[j] = fillval;
                j += 1;
            }
        }
        dst[0x43] = 0;
    } else {
        dst[4] = 0;
        dst[5] = 0;
        dst[0x45] = 0;
        dst[7] = 0;
        dst[0x43] = 0;
    }
}

/* 0x103193d0 - pitch/energy candidate refinement */
int32_t b25_refine_pitch(int32_t param_1, int16_t *out_r, int16_t *out_f3, int16_t *out_f4, int16_t *out_f5, int16_t p6, int16_t p7, int16_t p8, int16_t p9, int16_t p10)
{
    int16_t param_6;
    int32_t iv5 = 0;
    uint32_t uv2 = 0;
    int go_final = 0;
    uint32_t diff;
    int16_t s4;

    *out_r = p6;
    *out_f3 = 0;
    *out_f4 = 0;
    *out_f5 = 0;

    param_6 = p6;

    if (p8 == 0) {
        if (p9 == 0) {
            iv5 = 0;
            go_final = 1;
        } else {
            *out_r = p7;
            param_6 = p7;
            iv5 = w32_mul((int32_t)param_6, (int32_t)p10);
        }
    } else if (p9 == 0) {
        iv5 = w32_mul((int32_t)param_6, (int32_t)p10);
    } else {
        int16_t param_7 = p7;
        int32_t ip7 = (int32_t)p7;
        int32_t ip6 = (int32_t)p6;
        if (param_6 < (int16_t)((uint32_t)w32_mul(ip7, 0x9998) >> 16)
            && (int16_t)((uint32_t)w32_mul(ip7, 0x6666) >> 16) < param_6
            && param_6 < 0x4000) {
            param_7 = (int16_t)(p7 >> 1);
            *out_f4 = 1;
        } else if ((int16_t)((uint32_t)w32_mul(ip6, 0x6aaa) >> 16) < param_7
                   && param_7 < (int16_t)((uint32_t)w32_mul(ip6, (int32_t)0xa000u) >> 16)
                   && param_7 < 0x4000) {
            param_7 = (int16_t)((uint32_t)w32_shl(ip7, 0x11) >> 16);
            *out_f5 = 1;
        }
        *out_f3 = (int16_t)((uint32_t)w32_add(w32_mul((int32_t)param_7, 0x10000), w32_mul(ip6, -0x10000)) >> 16);
        iv5 = w32_mul((int32_t)(int16_t)((uint32_t)w32_add(w32_shl((int32_t)param_7, 16) >> 1,
                                                          w32_mul(ip6, 0x10000) >> 1) >> 16),
                      (int32_t)p10);
    }

    if (!go_final) {
        uv2 = (uint32_t)w32_add(w32_mul(iv5, 2) >> 4, param_1);
        if (uv2 == 0) {
            iv5 = 0;
            uv2 = 0xf;
        } else {
            uint32_t nrm = (uint32_t)b25_norm_shift_batch1(uv2) & 0xffff;
            iv5 = (int32_t)w32u_shl(uv2, nrm & 0x1f);
            uv2 = (0xfu - nrm) & 0xffff;
        }
    }

    diff = uv2 - 0xfu;
    s4 = (int16_t)diff;
    return b25_shift_i32_batch1(iv5, s4);
}

/* 0x10311b00 - unpack p2-1 nibbles from the packed table */
void b25_unpack_nibbles(uint16_t *out, int16_t p2, const int16_t *table)
{
    int16_t s8 = (int16_t)(p2 - 2);
    uint32_t uv5 = (uint32_t)(w32_mul((int32_t)p2 - 9, (int32_t)p2 + 6) >> 1);
    int16_t uv3_init = (int16_t)((int16_t)(uint16_t)uv5 >> 2);
    int32_t iv2 = (int32_t)((uv5 & 0xffff) + (uint32_t)(int32_t)uv3_init * 0xfffffffcu);
    int32_t iv7 = (int32_t)uv3_init + 1;
    uint32_t shift0 = ((uint32_t)((uint8_t)iv2 & 7)) << 2;
    uint16_t uv3 = (uint16_t)((int32_t)table[(size_t)uv3_init] >> shift0);
    size_t oi = 0;
    while (s8 >= 0) {
        uint16_t uv4 = (uint16_t)(uv3 & 0xf);
        uv3 = (uint16_t)((int16_t)uv3 >> 4);
        iv2 += 1;
        out[oi] = uv4;
        oi += 1;
        s8 -= 1;
        if ((int16_t)iv2 > 3) {
            int16_t s6 = (int16_t)iv7;
            iv7 += 1;
            uv3 = (uint16_t)table[(size_t)s6];
            iv2 = 0;
        }
    }
}

/* ======================= from batch3 ======================= */

/* index of the highest set bit, or 31 for zero */
int32_t b25_top_bit_batch3(uint32_t v)
{
    return 31 - (int32_t)(lz32(v) & 31);
}

/* block exponent */
int32_t b25_norm_exp_batch3(uint32_t x)
{
    return b25_norm_shift_batch1(x);
}

/* the library's variable shift */
int32_t b25_shift32_al2b2(int32_t v, int16_t s)
{
    return b25_shift_i32_batch1(v, s);
}

/* 32-bit saturating add */
int32_t b25_sat_add32(int32_t a, int32_t b)
{
    int32_t r = w32_add(a, b);
    if (((r ^ a) & (r ^ b)) < 0) {
        return (int32_t)(0x7fffffffu + (uint32_t)(a < 0));
    } else {
        return r;
    }
}

/* high half of a 32-bit accumulator as a Q15 word */
int16_t b25_high16_batch3(int32_t x)
{
    return (int16_t)(uint16_t)((uint32_t)x >> 16);
}

/* 0x10381370 - 64-entry scalar codebook */
const int16_t DSP_GAIN_CODEBOOK[64] = {
    -9917, -9102, -8311, -7440, -6597, -5828, -5081, -4272, -3370, -2903, -2583, -2305,
    -1962, -1601, -1138, -711, -302, 57, 433, 795, 1132, 1510, 1909, 2333,
    2705, 3038, 3376, 3689, 3979, 4339, 4754, 5129, 5435, 5695, 5991, 6300,
    6596, 6969, 7342, 7751, 8101, 8511, 8835, 9102, 9375, 9698, 10055, 10415,
    10762, 11084, 11403, 11752, 12123, 12468, 12863, 13239, 13615, 13997, 14382, 14770,
    15301, 15849, 16640, 17809,
};

/* 0x1037f978 - one full period of round(32767 cos(pi k / 256)) */
const int16_t DSP_TWIDDLE[512] = {
    32767, 32766, 32758, 32746, 32729, 32706, 32679, 32647, 32610, 32568, 32522, 32470,
    32413, 32352, 32286, 32214, 32138, 32058, 31972, 31881, 31786, 31686, 31581, 31471,
    31357, 31238, 31114, 30986, 30853, 30715, 30572, 30425, 30274, 30118, 29957, 29792,
    29622, 29448, 29269, 29086, 28899, 28707, 28511, 28311, 28106, 27897, 27684, 27467,
    27246, 27020, 26791, 26557, 26320, 26078, 25833, 25583, 25330, 25073, 24812, 24548,
    24279, 24008, 23732, 23453, 23170, 22884, 22595, 22302, 22006, 21706, 21403, 21097,
    20788, 20475, 20160, 19841, 19520, 19195, 18868, 18538, 18205, 17869, 17531, 17190,
    16846, 16500, 16151, 15800, 15447, 15091, 14733, 14373, 14010, 13646, 13279, 12910,
    12540, 12167, 11793, 11417, 11039, 10660, 10279, 9896, 9512, 9127, 8740, 8351,
    7962, 7571, 7180, 6787, 6393, 5998, 5602, 5205, 4808, 4410, 4011, 3612,
    3212, 2811, 2411, 2009, 1608, 1206, 804, 402, 0, -402, -804, -1206,
    -1608, -2009, -2411, -2811, -3212, -3612, -4011, -4410, -4808, -5205, -5602, -5998,
    -6393, -6787, -7180, -7571, -7962, -8351, -8740, -9127, -9512, -9896, -10279, -10660,
    -11039, -11417, -11793, -12167, -12540, -12910, -13279, -13646, -14010, -14373, -14733, -15091,
    -15447, -15800, -16151, -16500, -16846, -17190, -17531, -17869, -18205, -18538, -18868, -19195,
    -19520, -19841, -20160, -20475, -20788, -21097, -21403, -21706, -22006, -22302, -22595, -22884,
    -23170, -23453, -23732, -24008, -24279, -24548, -24812, -25073, -25330, -25583, -25833, -26078,
    -26320, -26557, -26791, -27020, -27246, -27467, -27684, -27897, -28106, -28311, -28511, -28707,
    -28899, -29086, -29269, -29448, -29622, -29792, -29957, -30118, -30274, -30425, -30572, -30715,
    -30853, -30986, -31114, -31238, -31357, -31471, -31581, -31686, -31786, -31881, -31972, -32058,
    -32138, -32214, -32286, -32352, -32413, -32470, -32522, -32568, -32610, -32647, -32679, -32706,
    -32729, -32746, -32758, -32766, -32767, -32766, -32758, -32746, -32729, -32706, -32679, -32647,
    -32610, -32568, -32522, -32470, -32413, -32352, -32286, -32214, -32138, -32058, -31972, -31881,
    -31786, -31686, -31581, -31471, -31357, -31238, -31114, -30986, -30853, -30715, -30572, -30425,
    -30274, -30118, -29957, -29792, -29622, -29448, -29269, -29086, -28899, -28707, -28511, -28311,
    -28106, -27897, -27684, -27467, -27246, -27020, -26791, -26557, -26320, -26078, -25833, -25583,
    -25330, -25073, -24812, -24548, -24279, -24008, -23732, -23453, -23170, -22884, -22595, -22302,
    -22006, -21706, -21403, -21097, -20788, -20475, -20160, -19841, -19520, -19195, -18868, -18538,
    -18205, -17869, -17531, -17190, -16846, -16500, -16151, -15800, -15447, -15091, -14733, -14373,
    -14010, -13646, -13279, -12910, -12540, -12167, -11793, -11417, -11039, -10660, -10279, -9896,
    -9512, -9127, -8740, -8351, -7962, -7571, -7180, -6787, -6393, -5998, -5602, -5205,
    -4808, -4410, -4011, -3612, -3212, -2811, -2411, -2009, -1608, -1206, -804, -402,
    0, 402, 804, 1206, 1608, 2009, 2411, 2811, 3212, 3612, 4011, 4410,
    4808, 5205, 5602, 5998, 6393, 6787, 7180, 7571, 7962, 8351, 8740, 9127,
    9512, 9896, 10279, 10660, 11039, 11417, 11793, 12167, 12540, 12910, 13279, 13646,
    14010, 14373, 14733, 15091, 15447, 15800, 16151, 16500, 16846, 17190, 17531, 17869,
    18205, 18538, 18868, 19195, 19520, 19841, 20160, 20475, 20788, 21097, 21403, 21706,
    22006, 22302, 22595, 22884, 23170, 23453, 23732, 24008, 24279, 24548, 24812, 25073,
    25330, 25583, 25833, 26078, 26320, 26557, 26791, 27020, 27246, 27467, 27684, 27897,
    28106, 28311, 28511, 28707, 28899, 29086, 29269, 29448, 29622, 29792, 29957, 30118,
    30274, 30425, 30572, 30715, 30853, 30986, 31114, 31238, 31357, 31471, 31581, 31686,
    31786, 31881, 31972, 32058, 32138, 32214, 32286, 32352, 32413, 32470, 32522, 32568,
    32610, 32647, 32679, 32706, 32729, 32746, 32758, 32766,
};

/* 0x10311a00 - advance a saturating frame counter held at state +0x86 */
void b25_bump_frame_counter(int16_t *state)
{
    int16_t *c = &state[DSP_FRAME_COUNTER_WORD];
    if (*c < 100) {
        *c = (int16_t)(*c + 1);
    }
}

/* 0x10310250 - fold n one-bit-per-word values into a single word, MSB first */
uint16_t b25_pack_bits_i16(const int16_t *bits, int16_t n)
{
    uint16_t acc = 0;
    if (n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            acc = (uint16_t)(w16u_mul(acc, 2) | (uint16_t)bits[i]);
        }
    }
    return acc;
}

/* 0x10310250 - u16-payload spelling of pack_bits_i16 */
uint16_t b25_pack_bits(const uint16_t *bits, int16_t n)
{
    uint16_t acc = 0;
    if (n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            acc = (uint16_t)(w16u_mul(acc, 2) | bits[i]);
        }
    }
    return acc;
}

/* 0x10317280 - Q15 scale of a vector */
void b25_scale_vec(int16_t *dst, const int16_t *src, int16_t n, int16_t coef)
{
    if (n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t p = w32_mul(w32_mul((int32_t)src[i], (int32_t)coef), 2);
            dst[i] = b25_high16_batch3(p);
        }
    }
}

/* 0x1030b5e0 - copy n words */
size_t b25_copy16_batch3(int16_t *dst, const int16_t *src, int16_t n)
{
    size_t nn;
    if (n <= 0) {
        return 0;
    }
    nn = (size_t)n;
    memcpy(dst, src, nn * sizeof(int16_t));
    return nn;
}

/* 0x1030aec0 - scale by 2^(exp-15) and round up to a multiple of 65536 */
int32_t b25_scale_ceil16(int32_t x, int16_t exp)
{
    int16_t s = w16_sub(exp, 0xf);
    int32_t v = b25_shift32_al2b2(x, s);
    int32_t r = w32_add(v, 0xffff);
    if (((r ^ v) & r) < 0) {
        r = (int32_t)(0x7fffffffu + (uint32_t)(v < 0));
    }
    return (int32_t)((uint32_t)r & 0xffff0000u);
}

/* 0x10318aa0 - block exponent of the largest unflagged entry */
int32_t b25_peak_exp_unflagged(const int16_t *vals, const int16_t *flags, int16_t n)
{
    int32_t best = 0;
    uint32_t m;
    int32_t i;
    if (n > 0) {
        size_t k;
        for (k = 0; k < (size_t)n; k++) {
            int32_t v = w32_mul((int32_t)vals[k], 0x10000);
            if (flags[k] == 0 && best < v) {
                best = v;
            }
        }
    }
    if (best == 0) {
        return 0;
    }
    m = best < 0 ? ~(uint32_t)best : (uint32_t)best;
    i = b25_top_bit_batch3(m);
    return w32_neg((int32_t)((0x1eu - (uint32_t)i) & 0xffff));
}

/* 0x1030b430 - signed 32/16 division, magnitude-first */
int32_t b25_div32(int32_t num, int16_t den)
{
    return b25_div32_common(num, den, 0);
}

/* body shared by div32 and div32_half */
int32_t b25_div32_common(int32_t num, int16_t den, uint32_t q_shift)
{
    int32_t sign = (int32_t)den ^ num;
    int32_t n, d, q;
    int16_t dd;
    if (num < 0) {
        n = (num == INT32_MIN) ? INT32_MAX : -num;
    } else {
        n = num;
    }
    if (den < 0) {
        dd = (den == INT16_MIN) ? INT16_MAX : (int16_t)(-den);
    } else {
        dd = den;
    }
    d = (int32_t)dd;
    q = (n < d) ? 0 : ((n / d) >> q_shift);
    if (sign < 0) {
        return -q;
    } else {
        return q;
    }
}

/* 0x1030a600 - div32 with the quotient halved */
int32_t b25_div32_half(int32_t num, int16_t den)
{
    return b25_div32_common(num, den, 1);
}

/* 0x103101b0 - saturating variable shift */
int32_t b25_shl_sat(int32_t x, int16_t s)
{
    uint8_t b;
    int32_t mask, kept;
    if (s < 0) {
        if (s < -0x1e) {
            return x >> 31;
        } else {
            return x >> ((uint32_t)(uint8_t)(int16_t)(-s) & 0x1f);
        }
    }
    b = (uint8_t)s;
    mask = (int32_t)(0xffffffffu << ((0x1fu - (uint32_t)b) & 0x1f));
    kept = mask & x;
    if (x < 0) {
        kept = w32_sub(kept, mask);
    }
    if (kept != 0) {
        return (int32_t)(0x7fffffffu + (uint32_t)(x < 0));
    }
    return (int32_t)((uint32_t)x << ((uint32_t)b & 0x1f));
}

/* 0x10317200 - scale a vector by a Q15 coefficient and requantise */
void b25_scale_vec_shift(int16_t *dst, const int16_t *src, int16_t n, int16_t coef, int16_t exp_in, int16_t exp_out)
{
    int16_t s;
    size_t i;
    if (n <= 0) {
        return;
    }
    s = w16_sub(exp_in, exp_out);
    for (i = 0; i < (size_t)n; i++) {
        int32_t p = w32_mul(w32_mul((int32_t)src[i], (int32_t)coef), 2);
        dst[i] = b25_high16_batch3(b25_shift_i32_batch1(p, s));
    }
}

/* 32x16 fractional multiply keeping 32 bits */
int32_t b25_q15mul32(int32_t v, int32_t c)
{
    return w32_add(w32_mul((int32_t)b25_high16_batch3(v), w32_mul(c, 2)),
                   (int32_t)(((uint32_t)v & 0xffff) * (uint32_t)c) >> 15);
}

/* 0x1030b2d0 - square root of a block-floating-point value */
int32_t b25_sqrt_norm(int32_t x, int16_t *exp)
{
    int32_t n, m, t, acc;
    uint16_t e;
    if (x == 0) {
        return 0;
    }
    n = b25_norm_exp_batch3((uint32_t)x);
    e = (uint16_t)w16_sub(*exp, (int16_t)n);
    m = (int32_t)b25_high16_batch3((int32_t)((uint32_t)x << ((uint32_t)n & 0x1f)));

    t = (int32_t)b25_high16_batch3(
        w32_add(w32_mul(w32_add(w32_mul(m, -0x198a), (int32_t)0x38a58000u), 2), 0x8000));
    acc = (int32_t)((uint32_t)w32_add(w32_mul(w32_add(w32_mul(t, m), 0x141f8000), 2), 0x8000)
                    & 0xffff0000u);
    if ((e & 1) != 0) {
        acc = w32_mul((int32_t)b25_high16_batch3(acc), 0xb504);
    }
    acc = (int32_t)((uint32_t)w32_add(acc, 0x8000) & 0xffff0000u);
    *exp = (int16_t)(((int32_t)(int16_t)e + 1) >> 1);
    return acc;
}

/* 0x10318130 - leaky accumulate into a block-floating-point value */
void b25_track_level(int16_t *val, int16_t *exp, int16_t add, int16_t add_exp)
{
    int16_t e0 = *exp;
    int16_t m = add_exp <= e0 ? e0 : add_exp;
    int32_t a, b, sum, v;
    int16_t nrm, ex;

    a = b25_shift32_al2b2(w32_mul((int32_t)*val, 0xf334), w16_sub(e0, m));
    b = b25_shift32_al2b2(w32_mul((int32_t)add, 0xcccc), w16_sub(w16_sub(add_exp, m), 4));
    sum = w32_add(a, b);

    nrm = (sum == 0) ? (int16_t)0 : (int16_t)b25_norm_exp_batch3((uint32_t)sum);
    ex = w16_sub(m, nrm);
    v = (int32_t)((uint32_t)sum << ((uint32_t)(int32_t)nrm & 0x1f));
    if (v == 0 || ex < -0x10) {
        v = 0x7fff0000;
        ex = -0x11;
    }
    *val = b25_high16_batch3(v);
    *exp = ex;
}

/* 0x10312330 - nearest-neighbour search of the 64-entry GAIN_CODEBOOK */
int16_t b25_quantize_codebook64(int16_t *x)
{
    int32_t a = w32_mul((int32_t)*x, 0x10000);
    int32_t d0 = (int32_t)b25_high16_batch3(b25_sat_add32(a, w32_mul((int32_t)DSP_GAIN_CODEBOOK[0], -0x10000)));
    int32_t best = w32_mul(w32_mul(d0, d0), 2);
    int16_t best_i = 0;
    size_t i;

    for (i = 1; i < 64; i++) {
        int16_t d = b25_high16_batch3(b25_sat_add32(a, w32_mul((int32_t)DSP_GAIN_CODEBOOK[i], -0x10000)));
        int32_t metric;
        if (d == INT16_MIN) {
            metric = INT32_MAX;
        } else {
            metric = w32_mul(w32_mul((int32_t)d, (int32_t)d), 2);
        }
        if (metric < best) {
            best = metric;
            best_i = (int16_t)i;
        }
    }
    *x = DSP_GAIN_CODEBOOK[(size_t)best_i];
    return best_i;
}

/* 0x1030a660 - divide and renormalise */
int16_t b25_div_norm(int32_t num, int16_t num_exp, int16_t den, int16_t exp_bias, int16_t *out_exp)
{
    int32_t sign = (int32_t)den ^ num;
    int32_t n, d, v;
    int16_t dd, e, q, nrm, m;
    if (num < 0) {
        n = (num == INT32_MIN) ? INT32_MAX : -num;
    } else {
        n = num;
    }
    if (den < 0) {
        dd = (den == INT16_MIN) ? INT16_MAX : (int16_t)(-den);
    } else {
        dd = den;
    }
    d = (int32_t)dd;

    e = num_exp;
    if (w32_mul(d, 0x10000) <= n) {
        n >>= 1;
        e = w16_add(e, 1);
    }
    q = (int16_t)((n / d) >> 1);
    if (sign < 0) {
        q = w16_neg(q);
    }

    v = w32_mul((int32_t)q, 0x10000);
    nrm = (v == 0) ? (int16_t)0 : (int16_t)b25_norm_exp_batch3((uint32_t)v);
    m = b25_high16_batch3((int32_t)((uint32_t)v << ((uint32_t)(int32_t)nrm & 0x1f)));
    if (m == 0) {
        *out_exp = 0;
        return 0;
    }
    *out_exp = w16_sub(w16_sub(e, nrm), exp_bias);
    return m;
}

/* 0x10312620 - remap a packed voicing field onto eight fixed frequency bands */
uint32_t b25_map_voicing_bits(int16_t bits, int32_t nbands, int16_t w0)
{
    uint32_t acc = 0;
    int32_t ctr = 7;
    int32_t limit = 7;
    int32_t centre = 0x0c000000;
    int32_t doubled = 0;
    uint16_t last = 0;

    for (;;) {
        int32_t u = w32_mul(w32_sub(w32_mul((int32_t)b25_high16_batch3(centre), (int32_t)w0), 0xece6), 0x10);
        int32_t idx;
        if (u < 0) {
            idx = 0;
        } else {
            uint32_t e;
            int32_t m, sh;
            if (u == 0) {
                e = 0;
            } else {
                e = (0x1eu - (uint32_t)b25_top_bit_batch3((uint32_t)u)) & 0xffff;
            }
            m = (int32_t)b25_high16_batch3((int32_t)((uint32_t)u << (e & 0x1f)));
            sh = w32_sub((int32_t)((0xfu - e) & 0xffff), 0x13);
            idx = b25_shift32_al2b2(w32_mul(m, 0xb86c), (int16_t)sh);
        }

        idx = w32_sub(w32_sub(nbands, idx >> 16), 1);
        if ((int32_t)(int16_t)idx < limit) {
            idx = ctr;
        }
        ctr -= 1;
        centre = w32_add(centre, 0x10000000);
        doubled = w32_mul((int32_t)acc, 2);
        limit -= 1;
        last = (uint16_t)((uint16_t)((((int32_t)bits) >> ((uint32_t)idx & 0x1f)) & 1) | (uint16_t)doubled);
        acc = (uint32_t)last;

        if ((uint16_t)ctr >= 0x8000) {
            break;
        }
    }
    return ((uint32_t)(uint16_t)b25_high16_batch3(doubled) << 16) | (uint32_t)last;
}

/* 0x10319a80 - accumulate a linearly-faded copy of src into dst */
void b25_ramp_mix(int32_t *dst, const int32_t *src, int16_t step, int16_t step_exp, int16_t n)
{
    int32_t base, acc;
    size_t i;
    if (n <= 0) {
        return;
    }
    base = w32_mul((int32_t)step, 0x10000);
    acc = 0;
    for (i = 0; i < (size_t)n; i++) {
        int32_t x = src[i];
        int32_t g = (int32_t)b25_high16_batch3(acc);
        int32_t y = w32_add(
            w32_sub(w32_mul(w32_mul((int32_t)b25_high16_batch3(x), g), -2),
                    (int32_t)(((uint32_t)x & 0xffff) * (uint32_t)g) >> 15),
            x);
        dst[i] = b25_sat_add32(y, dst[i]);
        acc = b25_sat_add32(acc, b25_shift32_al2b2(base, step_exp));
    }
}

/* 0x1030a310 - add two block-floating-point values */
int16_t b25_add_norm(int16_t a, int16_t a_exp, int16_t b, int16_t b_exp, int16_t *out_exp)
{
    return b25_addsub_norm(a, a_exp, b, b_exp, out_exp, 0);
}

/* 0x1031a710 - one radix-2 butterfly stage over interleaved pairs */
void b25_fft_butterfly_stage(int16_t *sum, int16_t *dif, int16_t stride, int16_t n)
{
    b25_radix2_stage(sum, dif, stride, n, 1);
}

/* body shared by fft_butterfly_stage and fft_stage */
void b25_radix2_stage(int16_t *xe, int16_t *xo, int16_t stride, int16_t n, int scaled)
{
    size_t iters, it, b;
    int32_t st, j;
    uint32_t sh;
    int k;
    if (n <= 0) {
        return;
    }
    iters = (size_t)w16u_add((uint16_t)(w16u_sub((uint16_t)n, 1) >> 1), 1);
    st = (int32_t)stride;
    j = 0;
    sh = scaled ? 16u : 15u;

    b = 0;
    for (it = 0; it < iters; it++) {
        for (k = 0; k < 2; k++) {
            int32_t c = (int32_t)DSP_TWIDDLE[(size_t)j];
            int32_t s = (int32_t)DSP_TWIDDLE[(size_t)(j + 128)];
            int32_t yr = (int32_t)xo[b];
            int32_t yi = (int32_t)xo[b + 1];
            int32_t t = w32_sub(w32_mul(c, yr), w32_mul(s, yi));
            int32_t h = w32_mul((int32_t)xe[b], 0x10000) >> 1;
            int32_t u, h2;
            xe[b] = (int16_t)(w32_add(h, t) >> sh);
            xo[b] = (int16_t)(w32_sub(h, t) >> sh);

            u = w32_add(w32_mul(c, yi), w32_mul(s, yr));
            h2 = w32_mul((int32_t)xe[b + 1], 0x10000) >> 1;
            xe[b + 1] = (int16_t)(w32_add(h2, u) >> sh);
            xo[b + 1] = (int16_t)(w32_sub(h2, u) >> sh);

            j += st;
            b += 2;
        }
    }
}

static int32_t b25_dsp_a_clamp64(int64_t v)
{
    return (int32_t)(v < (int64_t)INT32_MIN ? (int64_t)INT32_MIN : v > (int64_t)INT32_MAX ? (int64_t)INT32_MAX : v);
}

/* resonator3 */
void b25_resonator3(int32_t *dst, const int32_t *src, int32_t *state, int16_t n)
{
    size_t i;
    if (n <= 0) {
        return;
    }
    for (i = 0; i < (size_t)n; i++) {
        int32_t x = src[i];
        int64_t acc1, acc2, acc3;
        int32_t sat1, s2, s4, s3, s5, out;

        acc1 = (int64_t)b25_q15mul32(state[1], 0x78ab) - (int64_t)state[0] + (int64_t)x;
        sat1 = b25_dsp_a_clamp64(acc1);

        state[1] = sat1;
        s2 = state[2];
        state[0] = x;
        s4 = state[4];
        s3 = state[3];
        state[2] = sat1;
        state[4] = s2;

        acc2 = acc1 + (int64_t)s4 - 2 * (int64_t)s2 + 2 * (int64_t)b25_q15mul32(s3, 0x7c20);

        s5 = state[5];
        state[5] = s3;
        acc3 = acc2 - (int64_t)b25_q15mul32(s5, 0x78af);
        out = b25_dsp_a_clamp64(acc3);

        state[3] = out;
        dst[i] = out;
    }
}

/* ======================= from batch4 ======================= */

/* variable left shift with the x86 5-bit count mask */
int32_t b25_shl_var(int32_t x, int16_t n)
{
    return w32_shl(x, (uint32_t)(int32_t)n & 0x1f);
}

/* variable arithmetic right shift for a negative nominal shift */
int32_t b25_shr_neg(int32_t x, int16_t n)
{
    if (n < -0x1e) {
        return x >> 31;
    } else {
        return x >> ((uint32_t)(int32_t)w16_neg(n) & 0x1f);
    }
}

/* signed shift by n */
int32_t b25_shift_var(int32_t x, int16_t n)
{
    if (n < 0) {
        return b25_shr_neg(x, n);
    } else {
        return b25_shl_var(x, n);
    }
}

/* bsr */
int32_t b25_msb(uint32_t x)
{
    return 31 - (int32_t)lz32(x);
}

/* 0x1e - msb(|x|) */
int16_t b25_norm_shift_batch4(int32_t x)
{
    uint32_t m;
    if (x == 0) {
        return 0;
    }
    m = x < 0 ? ~(uint32_t)x : (uint32_t)x;
    return (int16_t)(0x1e - b25_msb(m));
}

/* ETSI L_sub */
int32_t b25_l_sub(int32_t a, int32_t b)
{
    int32_t neg = w32_neg(b);
    int32_t r = w32_add(a, neg);
    if (((~b ^ r) & (r ^ a)) < 0) {
        if (a < 0) {
            return INT32_MIN;
        } else {
            return INT32_MAX;
        }
    } else {
        return r;
    }
}

/* 0x1030af10 - 16-bit mantissa of x at exponent exp, in the high half */
int32_t b25_hi16_at_exp(int32_t x, int16_t exp)
{
    int16_t s = w16_sub(exp, 0xf);
    int32_t v = (s < 0) ? b25_shr_neg(x, s) : b25_shl_var(x, s);
    return (int32_t)((uint32_t)v & 0xffff0000u);
}

/* 0x1030b230 - residual of x below the 16-bit mantissa at exponent exp */
int32_t b25_frac_at_exp(int32_t x, int16_t exp)
{
    int16_t s = w16_sub(exp, 0xf);
    int16_t hi;
    int32_t v, rem;
    int16_t back;
    if (s < 0) {
        if (s < -0x1e) {
            hi = (int16_t)(x >> 31);
        } else {
            hi = (int16_t)((x >> ((uint32_t)(int32_t)w16_neg(s) & 0x1f)) >> 16);
        }
    } else {
        hi = (int16_t)(b25_shl_var(x, s) >> 16);
    }

    v = w32_shl((int32_t)hi, 16);
    if (v < 0) {
        v = w32_add(v, 0x10000);
    }
    back = w16_sub(0xf, exp);
    v = (back < 0) ? b25_shr_neg(v, back) : b25_shl_var(v, back);

    rem = w32_sub(x, v);
    if (exp < 0) {
        return b25_shr_neg(rem, exp);
    } else {
        return b25_shl_var(rem, exp);
    }
}

/* 0x1037f948 - polynomial coefficients for log2_q */
const int16_t DSP_LOG2_C[6] = { 23170, -23636, 22285, -23636, 16714, -16384 };

/* 0x1030b100 - base-2 logarithm of x * 2^-exp, Q16.15 */
int32_t b25_log2_q(int16_t x, int16_t exp)
{
    int32_t acc = w32_shl((int32_t)x, 16);
    int16_t n = (acc == 0) ? (int16_t)0 : b25_norm_shift_batch4(acc);
    int32_t d = (int32_t)(int16_t)(w32_sub(b25_shl_var(acc, n), w32_shl((int32_t)DSP_LOG2_C[0], 16)) >> 16);
    int32_t t;

    t = w32_mul((int32_t)DSP_LOG2_C[1], d);
    t = w32_add(w32_add(w32_mul(t, 2), 0x8000) & (int32_t)0xffff0000u, w32_shl((int32_t)DSP_LOG2_C[2], 16));
    t = w32_mul((int32_t)(int16_t)(t >> 16), d);
    t = w32_add(w32_add(w32_mul(t, 2), 0x8000) & (int32_t)0xffff0000u, w32_shl((int32_t)DSP_LOG2_C[3], 16));
    t = w32_mul((int32_t)(int16_t)(t >> 16), d);
    t = w32_add((w32_add(w32_mul(t, 2), 0x8000) >> 1) & (int32_t)0xffff8000u, w32_shl((int32_t)DSP_LOG2_C[4], 16));
    t = w32_mul((int32_t)(int16_t)(t >> 16), d);
    t = w32_add(w32_add(w32_mul(t, 8), 0x20000) & (int32_t)0xfffc0000u, w32_shl((int32_t)DSP_LOG2_C[5], 16));

    return w32_add(t >> 15, w32_shl((int32_t)w16_sub(exp, n), 16));
}

/* 0x1031a520 - block exponent of n interleaved complex words */
int32_t b25_cplx_block_exp(const int16_t *x, int16_t n)
{
    int32_t max = 0x01000000;
    if (n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t re = (int32_t)x[2 * i];
            int32_t im = (int32_t)x[2 * i + 1];
            int32_t v = w32_add(w32_mul(re, re), w32_mul(im, im));
            if (max < v) {
                max = v;
            }
        }
    }
    if (max == 0) {
        return -1;
    }
    return (int32_t)(uint16_t)b25_norm_shift_batch4(max) - 1;
}

/* 0x1031fc50 - codebook search minimising summed squared error */
int16_t b25_vq_search(const int16_t *target, const int16_t *book, int16_t n, int16_t cand, int16_t stride)
{
    int16_t best = 0;
    int32_t min = INT32_MAX;
    int16_t k;
    if (cand <= 0) {
        return 0;
    }
    for (k = 0; k < cand; k++) {
        int32_t acc = 0;
        if (n > 0) {
            ptrdiff_t off = (ptrdiff_t)k * (ptrdiff_t)stride;
            ptrdiff_t i;
            for (i = 0; i < (ptrdiff_t)n; i++) {
                int32_t a = w32_shl((int32_t)target[(size_t)i], 16);
                int32_t b = w32_shl((int32_t)book[(size_t)(off + i)], 16);
                int16_t d = (int16_t)(b25_l_sub(a, b) >> 16);
                int32_t sq;
                if (d == INT16_MIN) {
                    sq = INT32_MAX;
                } else {
                    sq = w32_mul(w32_mul((int32_t)d, (int32_t)d), 2);
                }
                acc = s32_add(acc, sq);
            }
        }
        if (acc < min) {
            min = acc;
            best = k;
        }
    }
    return best;
}

/* 0x10310280 - unpack the low n bits of word, MSB first */
int16_t b25_unpack_bits(int16_t word, int16_t *dst, int16_t n)
{
    int16_t w = word;
    int16_t i = w16_sub(n, 1);
    while (i >= 0) {
        dst[(size_t)i] = (int16_t)(w & 1);
        w = (int16_t)(w >> 1);
        i -= 1;
    }
    return n;
}

/* 0x10312f30 - copy n words between two buffers in either direction */
int16_t b25_copy16_batch4(int16_t *dst, int16_t *src, int16_t n, int store)
{
    size_t i;
    if (n <= 0) {
        return 0;
    }
    for (i = 0; i < (size_t)n; i++) {
        if (store) {
            src[i] = dst[i];
        } else {
            dst[i] = src[i];
        }
    }
    return n;
}

/* 0x10314870 - copy 57 words with the first duplicated, then hold */
void b25_spread_hold(int16_t *dst, const int16_t *src, int16_t n)
{
    size_t i;
    dst[0] = src[0];
    for (i = 1; i <= 0x38; i++) {
        dst[i] = src[i - 1];
    }
    if (n > 0x39) {
        int16_t pad = src[0x37];
        for (i = 0x39; i < (size_t)n; i++) {
            dst[i] = pad;
        }
    }
}

/* 0x10314d10 - spread_hold with (dst, n, src) argument order */
void b25_spread_hold_alt(int16_t *dst, int16_t n, const int16_t *src)
{
    b25_spread_hold(dst, src, n);
}

/* 0x103131f0 - one pass of the bit de/interleaver */
int16_t b25_interleave_walk(int16_t *out, size_t *out_pos, size_t out_end, int16_t *data, ptrdiff_t *data_pos, const int16_t *steps, ptrdiff_t *step_pos, int16_t *count, ptrdiff_t base_pos, int16_t off, int16_t limit, int store)
{
    while (*out_pos < out_end) {
        int16_t s = w16_sub(steps[(size_t)*step_pos], off);
        int16_t d;
        if (s >= 0) {
            size_t idx = (size_t)(*data_pos + (ptrdiff_t)s);
            if (store) {
                data[idx] = out[*out_pos];
            } else {
                out[*out_pos] = data[idx];
            }
            *out_pos += 1;
        }
        d = steps[(size_t)*step_pos];
        *step_pos += 1;
        *count = w16_add(*count, 1);
        *data_pos += (ptrdiff_t)d;
        if ((int32_t)limit + 1 <= (int32_t)*count) {
            *count = 1;
            off = w16_sub(off, 1);
            *step_pos -= (ptrdiff_t)limit;
            *data_pos = base_pos;
        }
    }
    return off;
}

/* 0x1030c300 - fill the IMBE frame's bit-allocation descriptor */
void b25_imbe_bit_alloc(int16_t *t)
{
    int16_t total;
    int i;
    t[0] = 0;
    t[1] = 1;
    t[3] = 0;
    t[4] = 12;
    t[5] = 8;
    t[6] = 6;
    t[7] = 0x3e;
    t[8] = 0;
    t[9] = 0;
    t[10] = 0;
    t[11] = 0;
    t[12] = 0;

    total = w16_add(t[5], t[4]);
    t[2] = total;
    for (i = 0; i < 7; i++) {
        total = w16_add(total, t[6 + i]);
        t[2] = total;
    }
}

/* 0x1030bc70 - fill the AMBE frame's bit-allocation descriptor for mode */
void b25_ambe_bit_alloc(int16_t *t, int16_t mode)
{
    int16_t total;
    int i;
    mode = (int16_t)b25_iclamp(mode, 0, 4);
    t[3] = mode;
    t[0] = 1;
    t[1] = 1;
    t[4] = 5;
    t[5] = 7;
    t[6] = 5;
    t[7] = 9;
    t[8] = 7;
    t[9] = 5;
    t[10] = 4;
    t[11] = 4;
    t[12] = 3;
    switch (mode) {
    case 1:
        t[12] = 2;
        break;
    case 3:
        t[11] = 3;
        t[12] = 1;
        break;
    case 4:
        t[10] = 3;
        t[11] = 3;
        t[12] = 1;
        break;
    case 2:
        t[12] = 1;
        break;
    default:
        break;
    }

    total = w16_add(t[4], t[5]);
    t[2] = total;
    for (i = 0; i < 7; i++) {
        total = w16_add(total, t[6 + i]);
        t[2] = total;
    }
}

/* 0x103177d0 - expand a packed voicing decision word into one flag per harmonic */
void b25_voicing_expand(int16_t *dst, int32_t mask, int16_t step, int16_t n)
{
    int16_t bands[16];
    int32_t m = mask;
    int32_t inc;
    int k;
    for (k = 0; k < 16; k++) {
        bands[k] = (int16_t)((uint16_t)m & 3);
        m >>= 2;
    }

    inc = w32_shl((int32_t)step, 6) >> 4;
    if (n > 0) {
        int32_t acc = inc;
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int16_t lo = (int16_t)(acc >> 16);
            int16_t hi, v;
            if (lo > 0xf) {
                lo = 0xf;
            }
            hi = w16_add(lo, 1);
            v = bands[(size_t)lo];
            if (hi > 0xf) {
                hi = 0xf;
            }
            v = (int16_t)(v | bands[(size_t)hi]);
            if (v > 2) {
                v = 1;
            }
            dst[i] = v;
            acc = w32_add(acc, inc);
        }
    }
    if (n < 0x38) {
        size_t i;
        for (i = (size_t)(n > 0 ? n : 0); i < 0x38; i++) {
            dst[i] = 0;
        }
    }
}

/* 0x10319b80 - accumulate a Q31 sequence scaled by a linearly ramping gain */
void b25_mac_ramp(int32_t *dst, const int32_t *src, int16_t step, int16_t shift, int16_t n)
{
    int32_t base, phase;
    size_t i;
    if (n <= 0) {
        return;
    }
    base = w32_shl((int32_t)step, 16);
    phase = 0;
    for (i = 0; i < (size_t)n; i++) {
        int32_t s = src[i];
        int32_t g = (int32_t)(int16_t)(phase >> 16);
        int32_t lo = (int32_t)(((uint32_t)s & 0xffff) * (uint32_t)g) >> 15;
        int32_t hi = w32_mul(w32_mul((int32_t)(int16_t)(s >> 16), g), 2);
        dst[i] = s32_add(dst[i], w32_add(lo, hi));
        phase = s32_add(b25_shift_var(base, shift), phase);
    }
}

/* 0x1031aa20 - in-place radix-2 butterflies over pairs of complex words */
void b25_bfly_pairs(int16_t *x, int16_t n, int16_t mode)
{
    uint32_t cnt, k;
    uint32_t sh;
    size_t b;
    if (n <= 0) {
        return;
    }
    cnt = (uint32_t)w16u_add((uint16_t)((uint16_t)w16_sub(n, 1) >> 1), 1);
    switch (mode) {
    case -2: sh = 2; break;
    case 0: sh = 0; break;
    default: sh = 1; break;
    }
    b = 0;
    for (k = 0; k < cnt; k++) {
        int32_t a0 = (int32_t)x[b], a2 = (int32_t)x[b + 2];
        int32_t a1, a3;
        x[b] = (int16_t)((a0 + a2) >> sh);
        x[b + 2] = (int16_t)((a0 - a2) >> sh);
        a1 = (int32_t)x[b + 1];
        a3 = (int32_t)x[b + 3];
        x[b + 1] = (int16_t)((a1 + a3) >> sh);
        x[b + 3] = (int16_t)((a1 - a3) >> sh);
        b += 4;
    }
}

/* 0x1031a580 - one decimation FFT stage over an even/odd pair of halves */
void b25_fft_stage(int16_t *xe, int16_t *xo, int16_t stride, int16_t n)
{
    b25_radix2_stage(xe, xo, stride, n, 0);
}

/* 0x1030c700 - renormalise the harmonic magnitude vector in the decoder state */
void b25_refine_mags(int16_t *state)
{
    uint16_t l_raw = (uint16_t)state[2];
    int16_t l = (int16_t)l_raw;
    int16_t exps[56];
    int16_t mags[56];
    uint16_t u9, u7;
    int16_t f0, hi, max;
    int32_t acc;

    memset(exps, 0, sizeof(exps));
    memset(mags, 0, sizeof(mags));

    if (l > 0) {
        int16_t e0 = state[66];
        size_t i;
        for (i = 0; i < (size_t)l; i++) {
            exps[i] = e0;
            mags[i] = state[8 + i];
        }
    }
    u9 = l_raw;

    f0 = state[6];
    acc = w32_mul(w32_mul((int32_t)l, (int32_t)f0), 0x2000);
    hi = (int16_t)(acc >> 16);
    u7 = l_raw;

    while (hi > 0x4ccd) {
        int16_t e0 = state[66];
        int32_t v, m, prod, next;
        int16_t sh;
        u7 = w16u_sub(u7, 1);
        v = (int32_t)hi;
        m = (int32_t)(int16_t)(w32_mul(v, 0xd556) >> 16);
        prod = w32_mul(w32_mul(m, (int32_t)mags[(size_t)(int16_t)u7]), 2);
        sh = (prod == 0) ? (int16_t)0 : b25_norm_shift_batch4(prod);

        next = w32_sub(w32_shl(v, 16), w32_shl((int32_t)f0, 16) >> 3);
        exps[(size_t)(int16_t)u7] = w16_add(w16_sub(e0, sh), 1);
        mags[(size_t)(int16_t)u7] = (int16_t)(b25_shl_var(prod, sh) >> 16);
        hi = (int16_t)(next >> 16);
    }

    max = exps[0];
    if ((int16_t)u9 > 1) {
        size_t i;
        for (i = 1; i < (size_t)u9; i++) {
            if (max < exps[i]) {
                max = exps[i];
            }
        }
    }

    if ((int16_t)u9 > 0) {
        int16_t i = 0;
        for (;;) {
            int32_t v = w32_shl((int32_t)(uint16_t)mags[(size_t)i], 16);
            int16_t d = w16_sub(exps[(size_t)i], max);
            int16_t r;
            if (d < 0) {
                if (d < -0x1e) {
                    r = (int16_t)(mags[(size_t)i] >> 15);
                } else {
                    r = (int16_t)((v >> ((uint32_t)(int32_t)w16_neg(d) & 0x1f)) >> 16);
                }
            } else {
                r = (int16_t)(b25_shl_var(v, d) >> 16);
            }
            state[8 + (size_t)i] = r;
            i += 1;
            if (i >= state[2]) {
                break;
            }
        }
    }
    state[66] = max;
}
