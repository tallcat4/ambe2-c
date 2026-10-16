/* C port of blip25-vocoder engine/dsp.rs, lines 2208-end (part dsp_b). Bit-exact translation; see PORTING.md. */
#include "b25_internal.h"

/* ======================= from l1_batch1 ======================= */

/* index of the highest set bit, or 31 for zero */
int32_t b25_top_bit_al2b5(uint32_t v)
{
    return b25_top_bit_batch3(v);
}

/* block exponent (callers guard x == 0) */
int32_t b25_norm_exp_l1_batch1(int32_t x)
{
    return b25_norm_shift_batch1((uint32_t)x);
}

/* the library's variable shift */
int32_t b25_shift32_al3b2(int32_t v, int16_t s)
{
    return b25_shift_i32_batch1(v, s);
}

/* 0x10309d80 - dec_ctrl: latch the 52-byte control block into the decoder state */
void b25_dec_ctrl(int16_t *state, const int16_t *ctrl)
{
    const size_t D = 0x614 / 2;
    b25_copy16_batch3(state + D, ctrl, 14);
    state[D + 14] = ctrl[14];
    state[D + 15] = ctrl[15];
    state[D + 16] = ctrl[16];
    b25_copy16_batch3(state + D + 17, ctrl + 17, 7);
    state[D + 24] = ctrl[24];
    state[D + 25] = ctrl[25];
}

/* 0x1030b380 - square root of a block-floating-point value, rescaled and rounded */
int32_t b25_sqrt_to_exp(int32_t x, int16_t exp, int16_t target)
{
    int16_t e = exp;
    int32_t m = b25_sqrt_norm(x, &e);
    int16_t s = w16_sub(e, target);
    return w32_add(b25_shift32_al3b2(m, s), 0x8000) >> 16;
}

/* 0x10317310 - rescale a block of words from one block exponent to another */
void b25_scale_block_exp(int16_t *dst, const int16_t *src, uint16_t n, int32_t e_from, int32_t e_to)
{
    if ((int16_t)n > 0) {
        int16_t s = (int16_t)w32_sub(e_from, e_to);
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            dst[i] = (int16_t)(b25_shl_sat(w32_shl((int32_t)src[i], 16), s) >> 16);
        }
    }
}

/* 0x10319000 - ceiling integer part and negated Q15 residual */
void b25_ceil_and_residual(int16_t *out_int, int16_t *out_frac, int16_t x, int16_t exp)
{
    int32_t v = w32_shl((int32_t)x, 16);
    int16_t n = (int16_t)(b25_scale_ceil16(v, exp) >> 16);
    int16_t s;
    int32_t shifted;
    *out_int = n;
    s = w16_sub(exp, 0xf);
    shifted = b25_shift32_al3b2(v, s);
    *out_frac = (int16_t)(w32_shl(w32_sub(w32_shl((int32_t)n, 16), shifted), 15) >> 16);
}

/* 0x103111b0 - average two spectra into the first, clamp, hold the last value to 56 entries */
void b25_avg_and_extend(int16_t *a, const int16_t *b, uint16_t n)
{
    int16_t n16;
    int16_t last;
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t s = w32_add(w32_shl((int32_t)b[i], 16) >> 1, w32_shl((int32_t)a[i], 16) >> 1);
            int16_t v = (int16_t)(b25_shl_sat(s, 1) >> 16);
            if (v > 0x77ff) {
                v = 0x77ff;
            } else if (v < -0x7800) {
                v = -0x7800;
            }
            a[i] = v;
        }
    }
    n16 = (int16_t)n;
    last = a[(size_t)(int16_t)(n16 - 1)];
    if (n16 < 0x38) {
        size_t i;
        for (i = (size_t)n16; i < 0x38; i++) {
            a[i] = last;
        }
    }
}

/* 0x10315c60 - nearest codeword by Hamming distance over the 10-bit table */
void b25_nearest_codeword(uint16_t pattern, int16_t *best, int16_t *best_idx, int16_t from, int16_t to, const uint16_t *table)
{
    int16_t i;
    if (from > to) {
        return;
    }
    i = from;
    for (;;) {
        uint16_t x = (uint16_t)(table[(size_t)(uint16_t)i] ^ pattern);
        if (((uint32_t)x & 0xfc00u) == 0) {
            int16_t d = (int16_t)b25_popcount_low((uint32_t)x, 10);
            if (d < *best) {
                *best = d;
                *best_idx = i;
            }
        }
        if (i == to) {
            break;
        }
        i = w16_add(i, 1);
    }
}

/* 0x1030cc90 - build the per-harmonic voiced/unvoiced flag vector */
void b25_voicing_setup(const int16_t *cfg, int16_t *dst)
{
    switch (cfg[0]) {
    case 1: {
        uint32_t mask = (uint32_t)(uint16_t)cfg[4] | ((uint32_t)(uint16_t)cfg[5] << 16);
        b25_voicing_expand(dst, (int32_t)mask, cfg[6], cfg[2]);
        break;
    }
    case 2:
        b25_fill16(dst, 0, 0x38);
        break;
    case 3: {
        int32_t cls;
        b25_fill16(dst, 0, 0x38);
        cls = (int32_t)b25_frame_size_class(cfg[1]);
        if (cls == 0) {
            dst[(size_t)cfg[3]] = 1;
        } else if ((uint32_t)(cls - 1) < 3) {
            uint16_t u = (uint16_t)cfg[3];
            dst[(size_t)(u >> 8)] = 1;
            dst[(size_t)(u & 0xff)] = 1;
        }
        break;
    }
    default:
        break;
    }
}

/* 0x103125d0 - look up a pre-baked voicing pattern and expand it to dibits */
void b25_voicing_from_table(int16_t mode, uint32_t *out0, uint32_t *out1, int16_t idx, int32_t sh, const uint32_t *table)
{
    if (mode == 1) {
        uint32_t c = (uint32_t)w32_sub(7, sh) & 0x1f;
        int16_t i = (int16_t)(uint16_t)((uint32_t)(uint16_t)idx << c);
        b25_expand_dibits(out0, out1, table[(size_t)i]);
    } else {
        *out1 = 0;
        *out0 = 0;
    }
}

/* 0x103179d0 - ten-tap antisymmetric (Hilbert / differentiating) filter over a spectrum */
void b25_hilbert_diff(int16_t *out, const int16_t *src, uint16_t n, const int16_t *taps)
{
    int16_t n16;
    if ((int16_t)n > 0) {
        size_t base = 0x13;
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t acc = 0;
            size_t k;
            for (k = 0; k < 10; k++) {
                int32_t t = (int32_t)taps[k];
                int32_t hi = (int32_t)src[base + 2 + 2 * k];
                int32_t lo = (int32_t)src[base - 2 * k];
                acc = w32_sub(w32_add(acc, w32_mul(w32_mul(hi, t), 2)), w32_mul(w32_mul(lo, t), 2));
            }
            out[i] = (int16_t)(b25_frac_at_exp(acc, 1) >> 16);
            base += 1;
        }
    }
    n16 = (int16_t)n;
    b25_fill16(out + (size_t)n16, 0, w16_sub(0x38, n16));
}

/* 0x10313540 - convert a frequency to a harmonic index and return the residual */
int32_t b25_harmonic_index(int16_t *index, int16_t l, int16_t w, int16_t cap, int16_t enable)
{
    int32_t lv = w32_shl((int32_t)l, 16);
    uint16_t n1 = lv == 0 ? (uint16_t)0 : (uint16_t)b25_norm_exp_l1_batch1(lv);
    int16_t e1 = 0;
    int16_t recip = b25_div_norm(0x41000000, 8, (int16_t)((int32_t)((uint32_t)lv << ((uint32_t)n1 & 0x1f)) >> 16),
                                 (int16_t)w32_sub(0xf, (int32_t)n1), &e1);
    uint16_t recip_u = (uint16_t)recip;
    int32_t base, acc;
    int16_t t;

    if (enable != 0) {
        int32_t v = w32_add(w32_shl((int32_t)w, 16) >> 1, 0x20800000);
        uint16_t n2 = v == 0 ? (uint16_t)0 : (uint16_t)b25_norm_exp_l1_batch1(v);
        int16_t e2 = 0;
        int16_t r = b25_div_norm((int32_t)((uint32_t)v << ((uint32_t)n2 & 0x1f)), (int16_t)w32_sub(8, (int32_t)n2),
                                 (int16_t)recip_u, e1, &e2);
        int32_t sh = b25_shift32_al3b2(w32_shl((int32_t)r, 16), w16_sub(e2, 0xf));
        int16_t d = (int16_t)(sh >> 16);
        if ((int32_t)d > (int32_t)l - 1) {
            d = (int16_t)((int32_t)l - 1);
        } else if (d < 0) {
            d = 0;
        }
        if (d > cap) {
            d = cap;
        } else if (d < 0) {
            d = 0;
        }
        *index = d;
    }

    base = b25_shift32_al3b2(-0x41000000, w16_sub(-7, e1));
    t = (int16_t)(w32_add(w32_shl((int32_t)*index, 17), 0x10000) >> 16);
    acc = w32_add(base, w32_mul(w32_mul((int32_t)t, (int32_t)(int16_t)recip_u), 2));
    return w32_add(b25_shift32_al3b2(acc, w16_add(e1, 7)), 0x8000) >> 16;
}

/* 0x10319e30 - accumulate one harmonic's contribution into the synthesis buffer */
void b25_synth_harmonic(int32_t *phase, const int16_t *table, int16_t step, int32_t taper_phase, size_t *cursor, int32_t *dst, int16_t shift, int16_t taper_step, uint16_t n, const int16_t *cos_tbl)
{
    int32_t inc, tp;
    size_t p;
    uint16_t it;
    if ((int16_t)n <= 0) {
        return;
    }
    inc = w32_shl((int32_t)step, 16) >> 11;
    tp = taper_phase;
    p = *cursor;

    for (it = 0; it < n; it++) {
        int32_t acc = *phase;
        uint16_t hi;
        size_t idx;
        int16_t frac, d;
        int32_t t0, t1, v, v2, cur, r;
        *phase = w32_add(acc, inc);

        hi = (uint16_t)((uint32_t)acc >> 16);
        idx = (size_t)(hi & 0xff);
        frac = (int16_t)(w32_shl(w32_sub(acc, w32_shl((int32_t)(int16_t)hi, 16)), 15) >> 16);

        t0 = w32_shl((int32_t)table[idx], 16) >> 1;
        t1 = w32_shl((int32_t)table[idx + 1], 16) >> 1;
        d = (int16_t)(w32_sub(t1, t0) >> 16);
        v = w32_add(t0, w32_mul(w32_mul((int32_t)d, (int32_t)frac), 2));

        if (taper_step != 0) {
            int16_t c = (int16_t)b25_cos_lookup((int16_t)(w32_shl(tp, 10) >> 16), cos_tbl);
            int16_t g = (int16_t)(w32_shl(w32_sub(0x8000, (int32_t)c), 15) >> 16);
            int32_t lo = (int32_t)((uint32_t)v & 0xffff);
            int32_t vhi = (int32_t)(int16_t)(v >> 16);
            v = w32_add(w32_mul(lo, (int32_t)g) >> 15, w32_mul(w32_mul(vhi, (int32_t)g), 2));
            tp = w32_add(tp, w32_shl((int32_t)taper_step, 16));
        }

        v2 = b25_shift32_al3b2(v, shift);
        cur = dst[p];
        r = w32_add(cur, v2);
        dst[p] = ((r ^ cur) & (r ^ v2)) < 0 ? (int32_t)(0x7fffffffu + (uint32_t)(v2 < 0)) : r;
        p += 1;
    }
    *cursor = p;
}

/* ======================= from l1_batch2 ======================= */

/* 0x1e - msb(|x|) for the inlined block-exponent sequence */
int32_t b25_norm_shift_l1_batch2(uint32_t x)
{
    return 0x1e - b25_top_bit_l1_batch5(x);
}

/* block exponent: zero -> 0, else norm_shift_l1_batch2 truncated to 16 bits */
uint32_t b25_norm_exp_l1_batch2(uint32_t x)
{
    return (uint32_t)b25_nrm(x);
}

int16_t b25_high16_al2b1(int32_t x)
{
    return b25_high16_batch3(x);
}

/* 0x1030a890 - 2^(x * 2^(in_exp-15)), requantised to out_exp */
int32_t b25_pow2_scaled(int16_t x, int16_t in_exp, int16_t out_exp)
{
    int32_t v = b25_shift_i32_batch1(w32_shl((int32_t)x, 16), w16_sub(in_exp, 0xf));
    int16_t e = 0;
    int32_t m = b25_pow2_frac(v, &e);
    return w32_add(b25_shift_i32_batch1(m, w16_sub(e, out_exp)), 0x8000) >> 16;
}

/* inner helper of init_agc_config: write a 32-bit field as two words */
void b25_wr32(int16_t *st, size_t w, uint32_t v)
{
    st[w] = (int16_t)(uint16_t)v;
    st[w + 1] = (int16_t)(uint16_t)(v >> 16);
}

/* 0x1030bd40 - write the default contents of the 146-byte gain/AGC control block */
void b25_init_agc_config(int16_t *st)
{
    b25_wr32(st, 0, 0x00ff0001u);
    b25_wr32(st, 2, 0x0000000fu);
    b25_wr32(st, 4, 0);
    b25_wr32(st, 6, 0x00004027u);
    b25_fill16(st + 8, 0, 0x38);
    b25_wr32(st, 64, 0x38003333u);
    b25_wr32(st, 66, 0);
    b25_wr32(st, 68, 0);
    b25_fill16(st + 70, 0, 3);
}

/* 0x1030cd50 - harmonic/band count for a frame, by mode */
uint32_t b25_band_count_for_mode(int32_t mode, int16_t a, int16_t b, const int16_t *cos_tbl)
{
    if (mode == 0) {
        int32_t r = w32_sub(b25_scale_ceil16(w32_mul((int32_t)a, 0xa000), 0xc), 0x10000);
        if (r < 0) {
            return 0;
        }
        return (uint32_t)(uint16_t)(r >> 16);
    }
    if ((uint32_t)mode - 1u < 3) {
        int32_t idx = 300 + (int32_t)b + (int32_t)a * 2;
        return (uint32_t)(uint16_t)w16_sub(cos_tbl[(size_t)idx], 1);
    }
    return 0;
}

/* 0x103148e0 - the Q16 ratio num / den */
int32_t b25_ratio_q16(int16_t num, int16_t den)
{
    int32_t d32, dn, acc, e, v;
    uint32_t n;
    int16_t q, sh;
    if (num == den) {
        return 0x10000;
    }
    d32 = w32_shl((int32_t)den, 16);
    n = b25_norm_exp_l1_batch2((uint32_t)d32);
    dn = (int32_t)((uint32_t)d32 << (n & 0x1f));
    acc = w32_shl((int32_t)num, 16);
    e = -4;
    if (b25_high16_al2b1(dn) <= num) {
        acc >>= 1;
        e = -3;
    }
    q = (int16_t)b25_div32_half(acc, b25_high16_al2b1(dn));
    v = w32_shl((int32_t)q, 16);
    sh = (int16_t)(e - (int32_t)((0u - n - 4u) & 0xffff) - 0xf);
    return b25_shift_i32_batch1(v, sh);
}

/* 0x103173e0 - copy n words while rescaling each by 2^sh */
void b25_shift_copy16(int16_t *dst, const int16_t *src, uint16_t n, int16_t sh)
{
    size_t i;
    if ((int16_t)n <= 0) {
        return;
    }
    for (i = 0; i < (size_t)n; i++) {
        dst[i] = b25_high16_al2b1(b25_shift_i32_batch1(w32_shl((int32_t)src[i], 16), sh));
    }
}

/* 0x10319080 - a + 1/d, split into an integer part and a Q15 remainder */
void b25_add_recip_split(int16_t *int_out, int16_t *frac_out, int16_t a, int16_t a_exp, int16_t d)
{
    int32_t v = w32_shl((int32_t)d, 16);
    uint32_t n = b25_norm_exp_l1_batch2((uint32_t)v);
    int16_t e1 = 0;
    int16_t q = b25_div_norm(0x40000000, 1, b25_high16_al2b1((int32_t)((uint32_t)v << (n & 0x1f))),
                             (int16_t)(-4 - (int32_t)n), &e1);
    int16_t e2 = 0;
    int16_t m = b25_add_norm(a, a_exp, q, e1, &e2);
    int32_t acc = w32_shl((int32_t)m, 16);
    int16_t hi = b25_high16_al2b1(b25_hi16_at_exp(acc, e2));
    int32_t shifted;
    *int_out = hi;
    shifted = b25_shift_i32_batch1(acc, w16_sub(e2, 0xf));
    *frac_out = b25_high16_al2b1(w32_shl(w32_sub(shifted, w32_shl((int32_t)hi, 16)), 0xf));
}

/* 0x10311910 - resample a 56-entry envelope onto n points by linear interpolation */
int32_t b25_resample_lerp(int16_t *out, uint16_t n, const int16_t *table, int16_t phase)
{
    int32_t d = (int32_t)(int16_t)b25_div32_half(0x80000, (int16_t)n);
    int32_t t = (int32_t)(int16_t)(w32_shl((int32_t)phase, 0x19) >> 0x10);
    int32_t step = w32_mul(w32_mul(d, t), 2) >> 0xc;
    int32_t acc, ph;
    size_t s;
    if ((int16_t)n <= 0) {
        return 0;
    }

    acc = 0;
    ph = step;
    for (s = 0; s < (size_t)n; s++) {
        int16_t hi = b25_high16_al2b1(ph);
        int32_t v;
        if (hi >= 0 && hi < 0x37 && hi == 0) {
            v = w32_shl((int32_t)table[0], 16);
        } else {
            size_t idx = hi < 0x37 ? (size_t)(uint16_t)hi : 0x37;
            int32_t f = (int32_t)((uint32_t)(ph >> 1) & 0x7fff);
            int32_t prod = w32_mul(w32_mul(f, (int32_t)table[idx - 1]), 2);
            int32_t r = prod == INT32_MIN ? INT32_MAX : w32_neg(prod);
            v = w32_add(r, w32_mul(w32_add(w32_mul((int32_t)table[idx], f), w32_shl((int32_t)table[idx - 1], 0xf)), 2));
        }
        ph = w32_add(ph, step);
        out[s] = b25_high16_al2b1(v);
        acc = w32_add(acc, v >> 6);
    }
    return acc;
}

/* 0x103163b0 - dst[i] = 2^(src[i] / 1024), all brought to one block exponent */
void b25_pow2_vector(int16_t *dst, int16_t *out_exp, const int16_t *src, uint16_t n)
{
    int16_t exps[56];
    int16_t mx;
    memset(exps, 0, sizeof(exps));
    b25_fill16(dst, 0, (int16_t)n);
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            dst[i] = b25_high16_al2b1(b25_pow2_frac(w32_shl((int32_t)src[i], 16) >> 10, &exps[i]));
        }
    }
    mx = exps[0];
    if ((int16_t)n > 1) {
        size_t i;
        for (i = 1; i < (size_t)n && i < 56; i++) {
            int16_t e = exps[i];
            if (mx < e) {
                mx = e;
            }
        }
    }
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t x = (int32_t)((uint32_t)(uint16_t)dst[i] << 16);
            int16_t sh = w16_sub(exps[i], mx);
            dst[i] = b25_high16_al2b1(b25_shift_i32_batch1(x, sh));
        }
    }
    *out_exp = mx;
}

/* 0x1031a1b0 - one merge stage of the real-input FFT, in place */
int32_t b25_fft_merge_stage(int16_t *buf, int32_t count, int16_t log2n, int16_t variant, int16_t stage_flag)
{
    int16_t size = (int16_t)w16u_shl(1, (uint32_t)log2n & 0x1f);
    uint16_t n = (uint16_t)w16_sub((int16_t)(size >> 2), 1);
    int16_t stride = (int16_t)(uint16_t)(0x200 >> ((uint32_t)log2n & 0x1f));
    size_t sz = (size_t)size;
    size_t half;
    int32_t x;

    if (stage_flag == 0) {
        int32_t a = w32_shl((int32_t)buf[1], 16) >> 1;
        int32_t b = w32_shl((int32_t)buf[0], 16) >> 1;
        buf[0] = b25_high16_al2b1(w32_add(b, a));
        buf[1] = b25_high16_al2b1(w32_sub(b, a));
    } else if (variant == 0) {
        int32_t a = w32_shl((int32_t)buf[0], 16) >> 1;
        int32_t b = w32_shl((int32_t)buf[1], 16) >> 1;
        buf[0] = b25_high16_al2b1(w32_add(b, a));
        buf[sz] = b25_high16_al2b1(w32_sub(a, b));
        buf[sz + 1] = 0;
        buf[1] = 0;
    } else {
        int32_t a = w32_shl((int32_t)buf[0], 16) >> 2;
        int32_t b = w32_shl((int32_t)buf[sz], 16) >> 2;
        buf[0] = b25_high16_al2b1(w32_add(b, a));
        buf[1] = b25_high16_al2b1(w32_sub(a, b));
    }

    half = (size_t)((int32_t)size >> 1);
    {
        size_t p2_start = sz - 2 - half;
        if (variant == 0) {
            b25_butterfly_a(buf + 2, buf + half, p2_start, n);
        } else {
            b25_butterfly_b(buf + 2, buf + half, p2_start, n);
        }
    }
    b25_fft_butterfly_pass(buf, 2, sz - 2, stride, n);

    buf[half] = (int16_t)(w32_shl((int32_t)buf[half], 16) >> 17);
    x = w32_shl((int32_t)buf[half + 1], 16);
    buf[half + 1] = x == INT32_MIN ? (int16_t)(INT32_MAX >> 17) : (int16_t)(w32_neg(x) >> 17);
    return w32_add(count, 1);
}

/* 0x10317b80 - split a harmonic into its voiced and unvoiced gains */
void b25_voicing_gains(int16_t *u_mant, int16_t *u_exp, int16_t *v_mant, int16_t *v_exp, int16_t amp, int16_t amp_exp, int16_t cross, int16_t cross_exp, int16_t gain)
{
    int32_t a = (int32_t)amp;
    uint16_t common = (uint16_t)w16_add(w16_mul(amp_exp, 2), 1);
    int32_t e2 = b25_shift_i32_batch1(w32_mul(w32_mul(a, a), 2), w16_sub(w16_mul(amp_exp, 2), (int16_t)common));
    int16_t sum, dif;
    int32_t den, pv;
    int16_t nd, recip, e_common, nv, mv, nu, mu;
    uint32_t pu;

    if (cross == 0) {
        dif = b25_high16_al2b1(e2);
        sum = dif;
    } else {
        int32_t e3 = b25_shift_i32_batch1(w32_mul(w32_mul((int32_t)cross, (int32_t)cross), 2),
                                          w16_sub(w16_mul(cross_exp, 2), (int16_t)common));
        sum = b25_high16_al2b1(w32_add(e3, e2));
        dif = b25_high16_al2b1(w32_sub(e2, e3));
        if (dif == 0) {
            *u_mant = 0;
            *u_exp = 0;
            *v_mant = 0x7fff;
            *v_exp = 0;
            return;
        }
    }

    den = w32_mul(w32_mul((int32_t)b25_high16_al2b1(w32_mul(w32_mul((int32_t)dif, a), 2)), (int32_t)gain), 2);
    nd = (int16_t)b25_norm_exp_l1_batch2((uint32_t)den);
    recip = (int16_t)b25_div32_half(0x3cd013a9, b25_high16_al2b1((int32_t)((uint32_t)den << ((uint32_t)nd & 0x1f))));
    e_common = w16_add(w16_sub(w16_sub(nd, (int16_t)common), amp_exp), 4);

    pv = w32_mul(w32_mul((int32_t)sum, (int32_t)recip), 2);
    nv = (int16_t)b25_norm_exp_l1_batch2((uint32_t)pv);
    *v_exp = w16_neg(nv);
    mv = b25_high16_al2b1((int32_t)((uint32_t)pv << ((uint32_t)nv & 0x1f)));
    *v_mant = mv;
    if (mv != 0) {
        *v_exp = w16_add(w16_add(*v_exp, (int16_t)common), e_common);
    }

    pu = (uint32_t)w32_shl((int32_t)b25_high16_al2b1(w32_mul(w32_mul((int32_t)b25_high16_al2b1(w32_mul(w32_mul((int32_t)cross, a), 2)),
                                                                     (int32_t)recip), 2)), 0xf);
    nu = (int16_t)b25_norm_exp_l1_batch2(pu);
    *u_exp = w16_neg(nu);
    mu = b25_high16_al2b1((int32_t)(pu << ((uint32_t)nu & 0x1f)));
    *u_mant = mu;
    if (mu != 0) {
        *u_exp = w16_add(w16_add(w16_add(w16_add(*u_exp, amp_exp), e_common), cross_exp), 2);
    }
}

/* ======================= from l1_batch3 ======================= */

/* block exponent 0x1e - bsr(|x|) */
int32_t b25_norm_e(int32_t x)
{
    return b25_norm_shift_batch1((uint32_t)x);
}

/* norm_e in the u16 field form, zero -> 0 */
uint32_t b25_norm_u16(int32_t x)
{
    if (x == 0) {
        return 0;
    }
    return (uint32_t)b25_norm_e(x) & 0xffff;
}

/* MSVC __allshl (0x10305930) */
int64_t b25_allshl_l1_batch3(int64_t v, uint8_t count)
{
    return b25_allshl_l1_batch4(v, count);
}

/* 0x1030c8b0 - copy a decoder parameter block, resetting the phase accumulator */
void b25_copy_params_reset_phase(int16_t *dst, const int16_t *src)
{
    dst[0x40] = 0;
    dst[0x41] = 0;
    dst[0x42] = src[0x42];
    b25_copy_params_l1_batch3(dst, src);
}

/* 0x1031f990 - copy a decoder parameter block: header plus the 0x38-word harmonic vector */
void b25_copy_params_l1_batch3(int16_t *dst, const int16_t *src)
{
    memcpy(dst, src, 4 * sizeof(int16_t));
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
    b25_copy16_batch3(dst + 8, src + 8, 0x38);
}

/* 0x1037fea8 - two 17-entry reference sequences used by tone_seq_track */
const uint8_t DSP_TONE_SEQ[0x25] = {
    21, 20, 20, 21, 21, 20, 20, 20, 21, 21, 22, 22, 22, 22, 22, 21, 21, 0, 0, 0, 21, 20, 20, 20,
    20, 20, 21, 21, 22, 22, 21, 21, 22, 22, 22, 21, 21,
};

/* 0x1030d2c0 - track a 17-symbol signalling sequence and drive its countdown */
int32_t b25_tone_seq_track(int16_t *sym, int16_t *state, int16_t mode)
{
    size_t base = mode == 1 ? 0 : 0x14;
    int16_t s, miss;
    size_t k;

    memmove(state + 1, state + 2, 0x10 * sizeof(int16_t));
    state[0x11] = *sym;
    s = *sym;
    if (s == 0xff) {
        state[0] = 0;
        return 0;
    }
    if (state[0] > 0 && s > 0x12 && s < 0x17) {
        uint32_t sh = (uint32_t)(0x20 - (int32_t)state[0]) & 0x1f;
        *sym = (int16_t)(0x15 - (int16_t)((0x02b4e4c3 >> sh) & 1));
        state[0] = (int16_t)(state[0] - 1);
        return 1;
    }
    miss = 0x11;
    state[0] = 0;
    for (k = 0; k < 0x11; k++) {
        if ((uint16_t)state[1 + k] == (uint16_t)DSP_TONE_SEQ[base + k]) {
            miss = (int16_t)(miss - 1);
        }
    }
    if (miss < 2) {
        state[0] = 0x20;
    }
    return 0;
}

/* 0x10310f10 - harmonic count L from the fundamental */
int16_t b25_harmonic_count(int16_t w0)
{
    int16_t l = (int16_t)b25_div32(0x3b39c, w0);
    if ((int32_t)w16_add(w16_mul(l, 2), 1) * (int32_t)w0 > 0x7ffff) {
        int16_t q = (int16_t)b25_div32(0x80000, w0);
        l = (int16_t)(w32_sub(w32_mul((int32_t)q, 0x10000), 1) >> 17);
    }
    if (l > 0x38) {
        return 0x38;
    }
    if (l < 9) {
        return 9;
    }
    return l;
}

/* 0x10311e50 - a[i] = a[i] + b[i] + k, clamped to the log-magnitude range */
void b25_vec_add3_clamp(int16_t *a, const int16_t *b, uint16_t n, int16_t k)
{
    size_t i;
    if ((int16_t)n <= 0) {
        return;
    }
    for (i = 0; i < (size_t)n; i++) {
        int32_t s = w32_add(w32_add(w32_shl((int32_t)a[i], 16) >> 2, w32_shl((int32_t)b[i], 16) >> 2),
                            w32_shl((int32_t)k, 16) >> 2);
        int16_t v = b25_high16_al2b1(b25_shl_sat(s, 2));
        a[i] = v >= 0x7800 ? (int16_t)0x77ff : v < -0x7800 ? (int16_t)-0x7800 : v;
    }
}

/* 0x10314d80 - normalised first moment of a vector */
int32_t b25_weighted_slope(const int16_t *v, int16_t n)
{
    int32_t acc = 0;
    int16_t i = 0;
    uint32_t na, ns;
    int32_t sq, qh, s;
    int16_t q, sw;
    if (n > 0) {
        while (i < n) {
            int16_t w;
            i = (int16_t)(i + 1);
            w = b25_high16_al2b1(w32_add(w32_mul((int32_t)w16_add(n, 1), 0x10000), w32_mul((int32_t)i, -0x20000)));
            acc = w32_add(acc, w32_mul(w32_mul((int32_t)w, (int32_t)v[(size_t)(i - 1)]), 2));
        }
    }
    na = b25_norm_u16(acc);
    sq = w32_mul(w32_mul((int32_t)n, (int32_t)n), 2);
    ns = b25_norm_u16(sq);
    q = (int16_t)b25_div32_half((int32_t)((uint32_t)acc << (na & 0x1f)) >> 1,
                                (int16_t)((int32_t)((uint32_t)sq << (ns & 0x1f)) >> 16));
    qh = w32_shl((int32_t)q, 16);
    s = ((int32_t)((0x13u - na) & 0xffff) - 0x1f) + (int32_t)ns;
    sw = (int16_t)s;
    if (sw >= 0) {
        return (int32_t)((uint32_t)qh << ((uint32_t)(uint8_t)sw & 0x1f)) >> 16;
    } else if (sw < -0x1e) {
        return qh >> 31;
    } else {
        return (qh >> ((uint32_t)(uint8_t)(0u - (uint8_t)sw) & 0x1f)) >> 16;
    }
}

/* 0x10317480 - convert a 32-bit accumulator vector to Q15 words at a common exponent */
void b25_round_block_i32(int16_t *dst, int32_t exp_dst, const int32_t *src, int32_t exp_src, uint16_t n)
{
    int16_t sh;
    size_t i;
    if ((int16_t)n <= 0) {
        return;
    }
    sh = (int16_t)w32_sub(exp_src, exp_dst);
    for (i = 0; i < (size_t)n; i++) {
        int32_t v = b25_shl_sat(src[i], sh);
        int32_t s = w32_add(v, 0x8000);
        if (((s ^ v) & s) < 0) {
            s = (int32_t)(0x7fffffffu + (uint32_t)(v < 0));
        }
        dst[i] = b25_high16_al2b1(s);
    }
}

/* read a 32-bit state field held at word w (low word first) */
int32_t b25_rd32_l1_batch3(const int16_t *s, size_t w)
{
    return (int32_t)((uint32_t)(uint16_t)s[w] | ((uint32_t)(uint16_t)s[w + 1] << 16));
}

void b25_wr32_l1_batch3(int16_t *s, size_t w, int32_t v)
{
    s[w] = (int16_t)v;
    s[w + 1] = (int16_t)(v >> 16);
}

/* 0x103170a0 - dot product with a 64-bit accumulator, returned normalised */
int32_t b25_dot_norm64(int16_t *out_exp, const int16_t *b, const int16_t *a, uint16_t n)
{
    int64_t acc = 0;
    int16_t sh;
    int64_t v;
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t p = w32_mul(w32_mul((int32_t)a[i], (int32_t)b[i]), 2);
            acc = w64_add(acc, (int64_t)p);
        }
    }
    sh = (int16_t)(uint16_t)b25_norm64((uint32_t)acc, (uint32_t)w64_shr(acc, 32));
    if (sh >= 0) {
        v = b25_allshl_l1_batch3(acc, (uint8_t)sh);
    } else {
        v = b25_allshr(acc, (uint8_t)(uint32_t)(-(int32_t)sh));
    }
    *out_exp = w16_neg(sh);
    return (int32_t)v;
}

/* 0x10318330 - window one half-frame out of the analysis ring and refill it with noise */
void b25_window_overlap(int16_t *out, int16_t *seed, int16_t *ring, const int16_t *win, uint16_t n, int16_t total)
{
    int16_t cnt = (int16_t)n;
    int32_t ni;
    int16_t kept, tail;
    int refill;
    if (cnt > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            out[i] = b25_high16_al2b1(w32_mul(w32_mul((int32_t)ring[i], (int32_t)win[i]), 2));
        }
    }
    ni = (int32_t)cnt;
    kept = 0;
    refill = 1;
    if (0x54 - ni > 0) {
        int32_t j = 0;
        for (;;) {
            kept = (int16_t)(kept + 1);
            ring[(size_t)j] = ring[(size_t)(j + ni)];
            j = (int32_t)kept;
            if (j >= 0x54 - ni) {
                break;
            }
        }
        if (kept > 0x53) {
            refill = 0;
        }
    }
    if (refill) {
        size_t p = (size_t)kept;
        uint16_t left = (uint16_t)w16_sub(0x54, kept);
        for (;;) {
            b25_rand_step(seed);
            ring[p] = *seed;
            p += 1;
            left = w16u_sub(left, 1);
            if (left == 0) {
                break;
            }
        }
    }
    if (cnt > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            out[(size_t)n + i] = b25_high16_al2b1(w32_mul(w32_mul((int32_t)ring[i], (int32_t)win[(size_t)n - i]), 2));
        }
    }
    tail = (int16_t)w32_sub((int32_t)total, w32_mul((int32_t)n, 2));
    b25_fill16(out + 2 * (size_t)n, 0, tail);
}

/* 0x10319570 - advance the interpolated pitch state by one half-frame */
void b25_pitch_advance(int32_t *out, int32_t *io, int16_t *dbl_sat, int16_t *dbl_wrap, int16_t w, int16_t *st)
{
    int32_t io0 = *io;
    int32_t prev = b25_rd32_l1_batch3(st, 0xa8);
    int32_t v = w32_mul(prev, 2);
    if (b25_high16_al2b1(v) > 0) {
        int32_t neg, u, wq;
        uint32_t nu, nw;
        int16_t ex, q;
        v = w32_sub(v, 0x10000);
        neg = v == INT32_MIN ? INT32_MAX : w32_neg(v);
        u = neg >> 1;
        nu = b25_norm_u16(u);
        wq = w32_shl((int32_t)w, 16);
        nw = b25_norm_u16(wq);
        ex = 0;
        q = b25_div_norm((int32_t)((uint32_t)u << (nu & 0x1f)), (int16_t)((0x0fu - nu) & 0xffff),
                         b25_high16_al2b1((int32_t)((uint32_t)wq << (nw & 0x1f))),
                         (int16_t)(uint16_t)(0u - 4u - nw), &ex);
        st[0xab] = ex;
        st[0xaa] = q;
    }
    b25_wr32_l1_batch3(st, 0xa8, v);
    *out = v;
    *dbl_wrap = b25_high16_al2b1(w32_shl((int32_t)*dbl_wrap, 0x11));
    *dbl_sat = b25_high16_al2b1(b25_shl_sat(w32_shl((int32_t)*dbl_sat, 16), 1));
    *io = w32_add(b25_rd32_l1_batch3(st, 0xa8), w32_mul(w32_sub(io0, prev), 2));
}

/* 0x1031a370 - the complete in-place radix-2 FFT */
int32_t b25_fft(int16_t *buf, int32_t exp_in, int16_t log2n)
{
    int16_t size = (int16_t)w32_shl(1, (uint32_t)((uint8_t)log2n & 0x1f));
    int16_t be = (int16_t)b25_cplx_block_exp(buf, size);
    uint32_t acc = 0;
    int16_t quarter, pass, stride;
    int32_t span;
    if (be < 2) {
        acc = 0xffffffffu;
    }
    if (be < 0) {
        acc = acc - 1u;
    }
    b25_bit_reverse(buf, size);
    b25_bfly_pairs(buf, size, (int16_t)acc);
    acc &= 0xffff;

    quarter = (int16_t)(size >> 2);
    span = 2;
    pass = 1;
    stride = 0x80;
    if (log2n > 1) {
        for (;;) {
            int scaled;
            int32_t half;
            be = w16_sub(be, 2);
            if ((pass & 1) == 0) {
                be = (int16_t)b25_cplx_block_exp(buf, size);
            }
            scaled = be <= 1;
            half = (int32_t)(int16_t)span;
            if (quarter > 0) {
                size_t off = 0;
                uint16_t q;
                for (q = 0; q < (uint16_t)quarter; q++) {
                    int16_t *lo = buf + off;
                    int16_t *hi = buf + off + (size_t)(half * 2);
                    if (scaled) {
                        b25_fft_butterfly_stage(lo, hi, stride, (int16_t)span);
                    } else {
                        b25_fft_stage(lo, hi, stride, (int16_t)span);
                    }
                    off += (size_t)(half * 4);
                }
            }
            span = w32_mul(span, 2);
            pass = w16_add(pass, 1);
            acc = acc + (scaled ? 0xffffu : 0u);
            stride = (int16_t)(stride >> 1);
            quarter = (int16_t)(quarter >> 1);
            if (pass >= log2n) {
                break;
            }
        }
    }
    return w32_sub(exp_in, (int32_t)acc);
}

/* ======================= from l1_batch4 ======================= */

/* shift_i32_batch1 followed by the round-to-i16 tail */
int32_t b25_shift_round16_l1_batch4(int32_t v, int16_t sh)
{
    return w32_add(b25_shift_i32_batch1(v, sh), 0x8000) >> 16;
}

int16_t b25_high16_al2b3(int32_t v)
{
    return b25_high16_batch3(v);
}

/* saturating 32-bit add (ETSI L_add) */
int32_t b25_sat_add_l1_batch4(int32_t a, int32_t b)
{
    return b25_sat_add32(a, b);
}

/* 0x1030ce00 - clamp a level in dB to [-60, +3] and map it to a linear gain word */
int16_t b25_db_to_gain(int16_t db)
{
    int32_t v;
    if (db > 3) {
        v = 3;
    } else if (db < -0x3c) {
        v = -0x3c;
    } else {
        v = (int32_t)db;
    }
    return (int16_t)(w32_add(w32_mul(v, 0x01542c00), 0x6bca01ff) >> 16);
}

/* 0x1030c920 - reset the 0x86-byte adaptive-level tracker to its power-on state */
void b25_init_level_tracker(int16_t *s)
{
    s[0] = 2;
    s[6] = 0x4000;
    s[7] = 0;
    s[1] = 0x00ff;
    s[2] = 0x000e;
    s[0x40] = 0;
    s[0x41] = 0;
    s[4] = 0;
    s[5] = 0;
    b25_fill16(s + 8, 0x6666, 0x38);
    s[0x42] = -0x12;
}

/* 0x1030d3c0 - map a frame-type code to a starting gain word */
int32_t b25_gain_for_rate(int16_t *out, uint16_t code, int16_t db)
{
    int16_t cls;
    if (w16u_sub(code, 5) < 0x76) {
        cls = 0;
    } else if (w16u_sub(code, 0x80) < 0x10) {
        cls = 1;
    } else if (w16u_sub(code, 0x90) < 0x10) {
        cls = 2;
    } else {
        cls = (int16_t)(3 + (int16_t)(w16u_sub(code, 0xa0) > 3));
    }
    if (cls != 0) {
        if ((int32_t)cls - 1 < 3) {
            int32_t g = (int32_t)b25_db_to_gain(db);
            *out = (int16_t)(w32_sub(w32_mul(g, 0x10000), 0x07ff0001) >> 16);
            return 1;
        }
        *out = 0;
        return 0;
    }
    *out = b25_db_to_gain(db);
    return 1;
}

/* 0x103102e0 - read n payload bits through a moving cursor */
uint16_t b25_read_field_l1_batch4(const uint16_t *bits, int32_t *pos, int16_t n)
{
    uint16_t v = b25_pack_bits(bits + (size_t)*pos, n);
    *pos = w32_add(*pos, (int32_t)n);
    return v;
}

/* 0x10310f80 - derive the pitch-search step and the harmonic-count limit from a pitch period */
void b25_set_pitch_params(int16_t *cfg, int16_t period)
{
    int16_t den = w16_add(w16_add(w16_mul(period, 4), 1), 0x9d);
    int32_t l32;
    int16_t l;
    cfg[6] = (int16_t)b25_div32_half(0x00800000, den);
    l32 = w32_mul((int32_t)w16_add((int16_t)(period >> 2), 10), 0xece8);
    l = (int16_t)(l32 >> 16);
    if (l > 0x38) {
        cfg[2] = 0x38;
        return;
    }
    if (l < 9) {
        l = 9;
    }
    cfg[2] = l;
}

/* 0x10317780 - leak a tracked level towards a new observation once voicing is dense enough */
uint16_t b25_smooth_level_if_voiced(uint16_t state, int16_t obs, uint32_t flags)
{
    if (b25_popcount_low(flags & 0x55555555u, 0x20) >= 8) {
        int32_t a = w32_mul((int32_t)obs, 0xcccc) >> 3;
        int32_t b = w32_mul((int32_t)(int16_t)state, 0xe666);
        return (uint16_t)((uint32_t)(w32_add(a, b) >> 16) & 0xffff);
    }
    return state;
}

/* 0x103196a0 - the Q15 ratio num / den */
int32_t b25_ratio_q15(int16_t num, int16_t den)
{
    int32_t dv = w32_shl((int32_t)den, 16);
    uint16_t nd = dv == 0 ? (uint16_t)0 : (uint16_t)((uint16_t)b25_norm_shift_batch1((uint32_t)dv) & 0xffff);
    int16_t dm = (int16_t)(w32_shl(dv, (uint32_t)nd & 0x1f) >> 16);
    int16_t d_bias = (int16_t)w16u_sub(w16u_sub(0, 4), nd);
    int32_t nv = w32_shl((int32_t)num, 16);
    uint16_t nn = nv == 0 ? (uint16_t)0 : (uint16_t)((uint16_t)b25_norm_shift_batch1((uint32_t)nv) & 0xffff);
    int16_t n_exp = (int16_t)w16u_sub(w16u_sub(0, 4), nn);
    int16_t e = 0;
    int16_t q = b25_div_norm(w32_shl(nv, (uint32_t)nn & 0x1f), n_exp, dm, d_bias, &e);
    return b25_shift_round16_l1_batch4(w32_shl((int32_t)q, 16), w16_sub(e, 0xf));
}

/* 0x10312400 - affine rescale of a vector: v[i] = coef * v[i] + bias */
void b25_scale_bias_vec(int16_t *v, int32_t num, int16_t n, int16_t coef)
{
    int32_t den = w32_shl((int32_t)n, 25) >> 16;
    int32_t q = b25_div32_half(num, (int16_t)den);
    int32_t p = w32_mul(w32_mul((int32_t)(int16_t)q, (int32_t)coef), 2);
    int32_t bias = (p == INT32_MIN ? INT32_MAX : w32_neg(p)) >> 1;
    if (n > 0) {
        size_t i;
        for (i = 0; i < (size_t)(uint16_t)n; i++) {
            int32_t t = w32_add(w32_mul(w32_mul((int32_t)v[i], (int32_t)coef), 2) >> 1, bias);
            v[i] = b25_high16_al2b3(b25_shl_sat(t, 1));
        }
    }
}

/* 0x10314e60 - blend a log-domain energy with a fixed floor to get a decision level */
int32_t b25_voicing_level(int16_t mant, int16_t exp, int16_t thresh, const int16_t *cfg)
{
    int16_t floor_;
    int32_t l, h;
    if (cfg[4] == 0 && thresh <= 0x666) {
        floor_ = (int16_t)(w32_sub(0x2c000000, w32_mul((int32_t)thresh, 0x64000)) >> 16);
    } else {
        floor_ = 0x400;
    }
    if (mant < 1) {
        l = (int32_t)0xfff60000u;
    } else {
        l = b25_log2_q(mant, w16_add(exp, 0x1e));
    }
    h = (int32_t)b25_high16_al2b3(b25_shl_sat(l, 10));
    return w32_add(w32_mul(h, 0xc000), w32_shl((int32_t)floor_, 16)) >> 16;
}

/* 0x10305930 - MSVC __allshl */
int64_t b25_allshl_l1_batch4(int64_t v, uint8_t count)
{
    uint32_t hi = (uint32_t)w64_shr(v, 32);
    uint32_t lo = (uint32_t)v;
    if (count >= 0x40) {
        return 0;
    }
    if (count < 0x20) {
        uint32_t n = (uint32_t)(count & 0x1f);
        uint32_t new_hi = n == 0 ? hi : ((hi << n) | (lo >> (32 - n)));
        return (int64_t)(((uint64_t)new_hi << 32) | (uint64_t)(lo << n));
    }
    return (int64_t)((uint64_t)(lo << ((uint32_t)count & 0x1f)) << 32);
}

/* 0x103820e8 - trunc(32768 / i) for i = 1..56, index 0 unused */
const int16_t DSP_RECIP_N[57] = {
    0, 32767, 16384, 10922, 8192, 6553, 5461, 4681, 4096, 3640, 3276, 2978, 2730, 2520, 2340, 2184,
    2048, 1927, 1820, 1724, 1638, 1560, 1489, 1424, 1365, 1310, 1260, 1213, 1170, 1129, 1092, 1057,
    1024, 992, 963, 936, 910, 885, 862, 840, 819, 799, 780, 762, 744, 728, 712, 697, 682, 668, 655,
    642, 630, 618, 606, 595, 585,
};

/* 0x1031f9e0 - resample a harmonic-amplitude vector through a cosine transform */
void b25_harmonic_dft(int16_t *buf, uint16_t n, int16_t count, const int16_t *cos_tbl)
{
    int16_t local[64];
    int16_t ni = (int16_t)n;
    int32_t recip;
    int16_t cnt;
    int32_t step_acc;
    size_t k;
    memset(local, 0, sizeof(local));
    b25_copy16_batch3(local, buf, ni);
    recip = (int32_t)DSP_RECIP_N[(size_t)ni];
    cnt = ni <= count ? ni : count;
    if (cnt <= 0) {
        return;
    }
    step_acc = 0;
    for (k = 0; k < (size_t)(uint16_t)cnt; k++) {
        int64_t acc = 0;
        int32_t phase = w32_add(step_acc >> 1, 0x40);
        int32_t step = (int32_t)(int16_t)(uint16_t)step_acc;
        int16_t s;
        int64_t r;
        int32_t hi, t;
        if (ni > 0) {
            size_t i;
            for (i = 0; i < (size_t)n; i++) {
                int32_t c = (int32_t)cos_tbl[(size_t)((phase >> 7) & 0x1ff)];
                acc = w64_add(acc, (int64_t)w32_mul(w32_mul((int32_t)local[i], c), 2));
                phase = w32_add(phase, step);
            }
        }
        s = (int16_t)b25_norm64((uint32_t)acc, (uint32_t)w64_shr(acc, 32));
        if (s < 0) {
            r = b25_allshr(acc, (uint8_t)((uint32_t)w32_neg((int32_t)s) & 0xff));
        } else {
            r = b25_allshl_l1_batch4(acc, (uint8_t)((uint32_t)(int32_t)s & 0xff));
        }
        hi = (int32_t)(int16_t)((int32_t)r >> 16);
        t = b25_shift_i32_batch1(w32_mul(w32_mul(hi, recip), 2), (int16_t)w16u_sub(0, (uint16_t)s));
        buf[k] = b25_high16_al2b3(w32_add(t, 0x8000));
        step_acc = w32_add(step_acc, recip);
    }
}

/* 0x103187f0 - accumulate one lag's cross-correlation and roll the history buffer */
void b25_correlate_and_shift(int32_t *acc, const int16_t *src, int16_t exp_new, int16_t *hist, int16_t *hist_exp, const int16_t *win, int16_t n)
{
    size_t n_us;
    if (n > 0) {
        size_t i;
        for (i = 0; i < (size_t)(uint16_t)n; i++) {
            int16_t a = hist[i];
            int16_t b = win[(size_t)n - i];
            int32_t p1 = (a == INT16_MIN && b == INT16_MIN) ? INT32_MAX : w32_mul(w32_mul((int32_t)a, (int32_t)b), 2);
            int32_t u2 = b25_shl_sat(p1, w16_sub(*hist_exp, 2));
            int16_t c = src[i];
            int16_t d = win[i];
            int32_t p2 = (c == INT16_MIN && d == INT16_MIN) ? INT32_MAX : w32_mul(w32_mul((int32_t)d, (int32_t)c), 2);
            int32_t u3 = b25_shl_sat(p2, w16_sub(exp_new, 2));
            int32_t s = b25_sat_add_l1_batch4(u2, u3);
            acc[i] = b25_sat_add_l1_batch4(s, acc[i]);
        }
    }
    n_us = (size_t)(uint16_t)n;
    b25_copy16_batch3(hist, src + n_us, n);
    b25_fill16(hist + n_us, 0, (int16_t)(0x54 - (int32_t)n));
    *hist_exp = exp_new;
}

/* ======================= from l1_batch5 ======================= */

/* index of the most significant bit of |x| (bsr; zero magnitude -> 0) */
int32_t b25_top_bit_l1_batch5(uint32_t x)
{
    uint32_t mag = (int32_t)x < 0 ? ~x : x;
    if (mag == 0) {
        return 0;
    }
    return 31 - (int32_t)lz32(mag);
}

/* guarded norm: zero -> 0, else 0x1e - bsr(|x|) truncated to 16 bits */
uint16_t b25_nrm(uint32_t x)
{
    if (x == 0) {
        return 0;
    }
    return (uint16_t)b25_norm_shift_l1_batch2(x);
}

/* top 16 bits of an accumulator as a signed word */
int16_t b25_high16_l1_batch5(int32_t x)
{
    return b25_high16_batch3(x);
}

/* x << 16, wrapping */
int32_t b25_up16_l1_batch5(int16_t x)
{
    return w32_shl((int32_t)x, 16);
}

/* 0x1030b1f0 - log2(x * 2^-exp) rendered at a caller-chosen Q point */
int32_t b25_log2_at_q(int16_t x, int16_t exp, int32_t q)
{
    int32_t l = b25_log2_q(x, exp);
    int32_t s = b25_shl_sat(l, (int16_t)w32_sub(0xf, q));
    return b25_sat_add_l1_batch4(s, 0x8000) >> 16;
}

/* 0x10312480 - (n + 2) / 3, capped at 12 */
int16_t b25_triads_capped(int16_t n)
{
    int16_t q = (int16_t)b25_div32((int32_t)n + 2, 3);
    if (q > 0xc) {
        return 0xc;
    }
    return q;
}

/* 0x103171d0 - arithmetic mean of n words */
int32_t b25_mean_i16(const int16_t *v, uint16_t n)
{
    int32_t acc = 0;
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            acc = w32_add(acc, (int32_t)v[i]);
        }
    }
    return b25_div32(acc, (int16_t)n);
}

/* 0x10314ee0 - log2 of a half-scaled word, as a Q11 result */
int32_t b25_log2_q11(int16_t x)
{
    int32_t v = w32_shl((int32_t)x, 15);
    uint16_t n = b25_nrm((uint32_t)v);
    int32_t l = b25_log2_q(b25_high16_l1_batch5(w32_shl(v, (uint32_t)n & 0x1f)), (int16_t)(5 - (int32_t)n));
    return w32_shl(l, 0xd) >> 0x11;
}

/* 0x10310310 - unpack n bits of word at a moving cursor */
void b25_emit_bits(size_t *cursor, int16_t *dst, int16_t n, int16_t word)
{
    size_t at = *cursor;
    int16_t written = b25_unpack_bits(word, dst + at, n);
    *cursor = at + (size_t)written;
}

/* 0x1030cb40 - weight a harmonic magnitude vector by (k*w0 / wmax)^2 */
void b25_weight_harmonics_by_freq_sq(int16_t *state, int16_t fmax)
{
    int16_t lim = fmax > 0x3333 ? (int16_t)0x3333 : fmax;
    int32_t v = b25_up16_l1_batch5(lim);
    uint16_t n = b25_nrm((uint32_t)v);
    int16_t e = 0;
    int16_t recip = b25_div_norm(0x40000000, 1, b25_high16_l1_batch5(w32_shl(v, (uint32_t)n & 0x1f)),
                                 (int16_t)(-4 - (int32_t)n), &e);
    size_t i = 0;
    int16_t f = state[6];
    while (f < lim) {
        int32_t p = w32_mul(w32_mul((int32_t)recip, (int32_t)f), 2);
        int16_t pn = p == 0 ? (int16_t)0 : (int16_t)(0x1e - b25_top_bit_l1_batch5((uint32_t)p));
        int32_t m = (int32_t)b25_high16_l1_batch5(w32_shl(p, (uint32_t)pn & 0x1f));
        int16_t sh = w16_mul(w16_sub(w16_sub(e, pn), 4), 2);
        int32_t sq = w32_mul(w32_mul(m, m), 2);
        int16_t g = b25_high16_l1_batch5(b25_shift_i32_batch1(sq, sh));

        state[8 + i] = b25_high16_l1_batch5(w32_mul(w32_mul((int32_t)g, (int32_t)state[8 + i]), 2));
        i += 1;
        f = b25_high16_l1_batch5(b25_sat_add_l1_batch4(b25_up16_l1_batch5(state[6]), b25_up16_l1_batch5(f)));
    }
}

/* 0x1030d490 - move the selected-harmonic marker and recompute w0 and L */
void b25_select_harmonic(int16_t *state, int16_t *flags, int16_t idx)
{
    size_t old = (size_t)state[3];
    int16_t held = state[8 + old];
    int32_t num, den;
    uint16_t n1, n2;
    int16_t e, m;
    state[8 + old] = 0;
    flags[old] = 0;
    state[3] = idx;
    state[8 + (size_t)idx] = held;
    flags[(size_t)state[3]] = 1;

    num = b25_up16_l1_batch5(state[1]);
    n1 = b25_nrm((uint32_t)num);
    den = b25_up16_l1_batch5(w16_add(state[3], 1));
    n2 = b25_nrm((uint32_t)den);

    e = 0;
    m = b25_div_norm(w32_shl(num, (uint32_t)n1 & 0x1f), (int16_t)(0xf - (int32_t)n1),
                     b25_high16_l1_batch5(w32_shl(den, (uint32_t)n2 & 0x1f)), (int16_t)(0x17 - (int32_t)n2), &e);
    e = w16_add(e, 4);
    state[6] = b25_high16_l1_batch5(b25_shift_i32_batch1(b25_up16_l1_batch5(m), e));

    if (state[2] <= state[3]) {
        state[2] = w16_add(state[3], 1);
    }
    if (state[2] > 0x38) {
        state[2] = 0x38;
    } else if (state[2] < 9) {
        state[2] = 9;
    }
}

/* 0x103178e0 - lay a spectral envelope into the analysis history block and extend it */
void b25_build_spectrum_history(int16_t *state, const int16_t *src, int16_t n)
{
    int32_t acc, end;
    int16_t first;
    size_t j;
    state[0x13] = 0;
    b25_copy16_batch3(state + 0x14, src, n);

    acc = b25_up16_l1_batch5(src[(size_t)(int16_t)(n - 1)]);
    first = w16_add(n, 1);
    if (first < 0x41) {
        size_t count = (size_t)((0x41u - ((uint32_t)(int32_t)n + 1u)) & 0xffff);
        size_t at = 0x13 + (size_t)first;
        size_t c;
        for (c = 0; c < count; c++) {
            acc = b25_sat_add_l1_batch4(acc, (int32_t)0xfa3b0000u);
            state[at] = b25_high16_l1_batch5(acc);
            at += 1;
        }
    }

    end = (int32_t)n + 0x14;
    if (0x41 < end) {
        size_t read = 0x52;
        int32_t i = 0x41;
        int16_t s = 0x41;
        for (;;) {
            int16_t v = state[read];
            read -= 1;
            s = w16_add(s, 1);
            state[0x13 + (size_t)i] = v;
            i = (int32_t)s;
            if (i >= end) {
                break;
            }
        }
    }

    for (j = 0; j < 0x13; j++) {
        state[0x12 - j] = state[0x14 + j];
    }
}

/* 0x10318960 - resample an m+1-point curve onto n+1 points by linear interpolation */
void b25_resample_linear(int16_t *out, int16_t n, const int16_t *src, int16_t m)
{
    int32_t vn = b25_up16_l1_batch5(n);
    uint16_t nn = vn != 0 ? b25_nrm((uint32_t)vn) : (uint16_t)0;
    int32_t vm = b25_up16_l1_batch5(m);
    uint16_t nm = b25_nrm((uint32_t)vm);
    int16_t e = 0;
    int16_t q = b25_div_norm(w32_shl(vm, (uint32_t)nm & 0x1f), w16_neg((int16_t)nm),
                             b25_high16_l1_batch5(w32_shl(vn, (uint32_t)nn & 0x1f)), w16_neg((int16_t)nn), &e);
    int32_t step = b25_shift_i32_batch1(b25_up16_l1_batch5(q), w16_sub(e, 0xf));

    out[0] = src[0];
    if (n > 1) {
        int32_t acc = step;
        size_t i;
        for (i = 1; i < (size_t)n; i++) {
            int32_t idx = (int32_t)b25_high16_l1_batch5(acc);
            int32_t f = (int32_t)(int16_t)((uint16_t)(acc >> 1) & 0x7fff);
            int32_t a = (int32_t)src[(size_t)idx];
            int32_t b = (int32_t)src[(size_t)(idx + 1)];
            out[i] = b25_high16_l1_batch5(w32_add(w32_mul(w32_add(w32_mul(b, f), w32_mul(a, 0x8000)), 2),
                                                  w32_mul(w32_mul(a, f), -2)));
            acc = w32_add(acc, step);
        }
    }
    out[(size_t)n] = src[(size_t)m];
}

/* 0x10312cb0 - interleave (or de-interleave) a frame's quantiser fields */
void b25_frame_bit_permute(int16_t *buf, const int16_t *cfg, int16_t store)
{
    int16_t img[192];
    int st = store != 0;
    int16_t b0, b1, b2, b3, b4, b5, b6, b7, b8;
    int16_t p1, p2, p3, p4, p5, p6, p7, q, q1, q2, q3;
    int16_t off[18], cnt[18];
    size_t cur, k;
    memset(img, 0, sizeof(img));
    if (!st) {
        b25_copy16_batch3(img, buf, cfg[2]);
    }

    b0 = cfg[4]; b1 = cfg[5]; b2 = cfg[6]; b3 = cfg[7];
    b4 = cfg[8]; b5 = cfg[9]; b6 = cfg[10]; b7 = cfg[11]; b8 = cfg[12];

    p1 = b1;
    p2 = w16_add(p1, b0);
    p3 = w16_add(p2, b2);
    p4 = w16_add(p3, b3);
    p5 = w16_add(p4, b4);
    p6 = w16_add(p5, b5);
    p7 = w16_add(p6, b6);
    q = w16_add(w16_add(w16_add(b3, b2), b0), b1);
    q1 = w16_add(q, b4);
    q2 = w16_add(q1, b5);
    q3 = w16_add(q2, b6);

    off[0] = 0;                                   cnt[0] = 4;
    off[1] = p1;                                  cnt[1] = 4;
    off[2] = p2;                                  cnt[2] = 4;
    off[3] = p3;                                  cnt[3] = 8;
    off[4] = p4;                                  cnt[4] = 4;
    off[5] = p5;                                  cnt[5] = 4;
    off[6] = p6;                                  cnt[6] = 3;
    off[7] = p7;                                  cnt[7] = 3;
    off[8] = w16_add(p7, b7);                     cnt[8] = 1;
    off[9] = w16_add(b1, 4);                      cnt[9] = (int16_t)(b0 - 4);
    off[10] = w16_add(w16_add(b0, b1), 4);        cnt[10] = (int16_t)(b2 - 4);
    off[11] = 4;                                  cnt[11] = (int16_t)(b1 - 4);
    off[12] = w16_add(q, 4);                      cnt[12] = (int16_t)(b4 - 4);
    off[13] = w16_add(w16_sub(q, b3), 8);         cnt[13] = (int16_t)w16u_sub((uint16_t)b3, 8);
    off[14] = w16_add(q1, 4);                     cnt[14] = (int16_t)(b5 - 4);
    off[15] = w16_add(q2, 3);                     cnt[15] = (int16_t)(b6 - 3);
    off[16] = w16_add(q3, 3);                     cnt[16] = (int16_t)(b7 - 3);
    off[17] = w16_add(w16_add(b7, q3), 1);        cnt[17] = (int16_t)(b8 - 1);

    cur = 0;
    for (k = 0; k < 18; k++) {
        int16_t moved = b25_copy16_batch4(buf + cur, img + (size_t)off[k], cnt[k], st);
        cur = cur + (size_t)moved;
    }

    if (st) {
        b25_copy16_batch3(buf, img, cfg[2]);
    }
}

/* 0x10319760 - solve y*(2*ac + k*y) = m for each of count harmonics */
void b25_solve_quad_ramp(int16_t *out, int16_t *exps, int32_t x0, int16_t a, int16_t k, int16_t c, uint16_t count)
{
    int32_t a2, c32, ac, c2;
    int16_t i;
    size_t slot;
    if ((int16_t)count <= 0) {
        return;
    }
    a2 = w32_mul(w32_mul((int32_t)a, (int32_t)a), 2);
    c32 = w32_mul((int32_t)c, 0x10000);
    ac = w32_mul(w32_mul((int32_t)a, (int32_t)c), 2);
    c2 = w32_mul(w32_mul((int32_t)c, (int32_t)c), 2);

    i = 0;
    for (slot = 0; slot < (size_t)count; slot++) {
        int32_t t, u, e_m, prod, m, kw, r, scaled;
        uint16_t n1, n2, n3, n4, n5, n6, n7, n8;
        int16_t e, s, g, v;
        i = w16_add(i, 1);
        t = w32_sub(w32_mul((int32_t)i, 0x10000), x0);
        n1 = b25_nrm((uint32_t)t);
        u = w32_mul(w32_mul((int32_t)b25_high16_l1_batch5(w32_shl(t, (uint32_t)n1 & 0x1f)), (int32_t)c), 2);
        n2 = b25_nrm((uint32_t)u);
        e_m = w32_add((int32_t)((0xfu - (uint32_t)n1) & 0xffff), w32_sub(0x10, (int32_t)n2));

        n3 = b25_nrm((uint32_t)a2);
        n4 = b25_nrm((uint32_t)c2);
        prod = w32_mul(w32_mul((int32_t)b25_high16_l1_batch5(w32_shl(c2, (uint32_t)n4 & 0x1f)),
                               (int32_t)b25_high16_l1_batch5(w32_shl(a2, (uint32_t)n3 & 0x1f))), 2);
        n5 = b25_nrm((uint32_t)prod);
        m = (int32_t)b25_high16_l1_batch5(w32_shl(u, (uint32_t)n2 & 0x1f));

        e = (int16_t)((((0u - (uint32_t)n3) - 8u) & 0xffff) + (0x1eu - (uint32_t)n4) - (uint32_t)n5);

        kw = w32_mul(w32_mul((int32_t)k, m), 2);
        n6 = b25_nrm((uint32_t)kw);
        s = b25_add_norm(b25_high16_l1_batch5(w32_shl(prod, (uint32_t)n5 & 0x1f)), e,
                         b25_high16_l1_batch5(w32_shl(kw, (uint32_t)n6 & 0x1f)),
                         (int16_t)w32_sub(w32_sub(e_m, (int32_t)n6), 4), &e);

        r = b25_sqrt_norm(b25_up16_l1_batch5(s), &e);
        n7 = r != 0 ? b25_nrm((uint32_t)r) : (uint16_t)0;
        e = w16_sub(e, (int16_t)n7);

        n8 = b25_nrm((uint32_t)ac);
        g = b25_add_norm(b25_high16_l1_batch5(w32_shl(ac, (uint32_t)n8 & 0x1f)), (int16_t)(0xbu - (uint32_t)n8),
                         b25_high16_l1_batch5(w32_shl(r, (uint32_t)n7 & 0x1f)), e, &e);

        v = b25_div_norm(w32_mul(m, 0x10000), (int16_t)e_m, g, e, &e);
        scaled = b25_shift_i32_batch1(b25_up16_l1_batch5(v), w16_sub(e, 0xf));
        if (w32_add(scaled, w32_mul((int32_t)c, -0x10000)) >= 0) {
            uint16_t n9 = b25_nrm((uint32_t)c32);
            e = (int16_t)((0xfu - (uint32_t)n9) & 0xffff);
            v = b25_high16_l1_batch5(w32_shl(c32, (uint32_t)n9 & 0x1f));
        }
        out[slot] = v;
        exps[slot] = e;
    }
}

/* ======================= pub mod tables ======================= */

/* 0x10381920 - dibit allocation for voicing_pattern: six rows of eight */
const int16_t DSPT_BIT_ALLOC[48] = {
    3, 2, 3, 0, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0, 2, 1, 2, 1, 2, 0, 0, 0, 1, 1, 2, 1, 1, 2,
    0, 0, 1, 1, 1, 1, 1, 1, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1,
};

/* 0x10381980 - the 56 pre-baked voicing patterns used for index < 0x38 */
const uint32_t DSPT_VOICING_LUT[56] = {
    0x00020000u, 0x00080000u, 0x000a0000u, 0x00200000u, 0x002a0000u, 0x00aa0000u, 0x02000000u,
    0x02800000u, 0x00008000u, 0x00000280u, 0x00000200u, 0x00000aa8u, 0x00000800u, 0x00000a00u,
    0x00000a80u, 0x000002a0u, 0x00000002u, 0x0000002au, 0x00000008u, 0x00000020u, 0x0000000au,
    0x000000aau, 0x00000080u, 0x000000a0u, 0x80000000u, 0x0a800000u, 0xa8000000u, 0x02a00000u,
    0xa0000000u, 0xaa000000u, 0x02aa0000u, 0x0a000000u, 0xaaaa0000u, 0x00800000u, 0x0aaa0000u,
    0xaa800000u, 0x08000000u, 0xaaa00000u, 0x2aaa0000u, 0xaaa80000u, 0x0000aaaau, 0x0000aa80u,
    0x0000a800u, 0x00002aaau, 0x0000a000u, 0x000002aau, 0x00000aaau, 0x0000aaa0u, 0xaaaaaaaau,
    0xaaaa0aaau, 0xa800a800u, 0x02aa02aau, 0x0aaa0aaau, 0xaaaaa800u, 0xa000a000u, 0x08000800u,
};

/* 0x10381cd8 - incremental swap list for the 8-point bit reversal */
const int16_t DSPT_BITREV8[5] = {2, 2, 8, 3, 3};

/* 0x10381ce4 - incremental swap list for the 16-point bit reversal */
const int16_t DSPT_BITREV16[13] = {6, 2, 16, 1, -9, 1, 15, 3, -5, 3, 7, 7, -3};

/* 0x10381d00 - incremental swap list for the 32-point bit reversal */
const int16_t DSPT_BITREV32[25] = {
    12, 2, 32, 1, -17, 1, 31, 3, -9, 1, -17, 1, 31, 3, -21, 3, 15, 3, -9, 3, 15, 7, -11, 7, 7,
};

/* 0x10381d38 - incremental swap list for the 64-point bit reversal */
const int16_t DSPT_BITREV64[57] = {
    28, 2, 64, 1, -33, 1, 63, 1, -81, 1, 63, 1, -33, 1, 63, 3, -41, 1, -33, 1, 63, 3, -17, 1,
    -33, 1, 63, 3, -53, 3, 31, 3, -17, 1, -33, 1, 63, 3, -41, 3, 31, 3, -17, 3, 31, 7, -27, 3,
    -17, 3, 31, 7, -9, 7, 15, 15, -5,
};

/* 0x10381db0 - incremental swap list for the 128-point bit reversal */
const int16_t DSPT_BITREV128[113] = {
    56, 2, 128, 1, -65, 1, 127, 1, -161, 1, 127, 1, -65, 1, 127, 3, -81, 1, -65, 1, 127, 1,
    -161, 1, 127, 1, -65, 1, 127, 3, -105, 1, -65, 1, 127, 3, -33, 1, -65, 1, 127, 3, -81, 1,
    -65, 1, 127, 3, -33, 1, -65, 1, 127, 3, -117, 3, 63, 3, -33, 1, -65, 1, 127, 3, -81, 3, 63,
    3, -33, 1, -65, 1, 127, 3, -105, 3, 63, 3, -33, 3, 63, 3, -81, 3, 63, 3, -33, 3, 63, 7,
    -59, 3, -33, 3, 63, 7, -17, 3, -33, 3, 63, 7, -41, 7, 31, 7, -17, 7, 31, 15, -21, 15, 15,
};
