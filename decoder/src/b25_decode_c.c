/* C port of blip25-vocoder src/engine/decode.rs lines 1-2844 (part decode_c). */
#include "b25_internal.h"

/* ======================= from aL2b1 ======================= */

/* 0x103813f0 - per-coefficient scale for the five higher-order coefficients */
const int16_t DEC_HOC_SCALE[5] = {20316, 13173, 11010, 9503, 8651};

/* 0x1038140e - step-size normaliser indexed by the number of bits */
const int16_t DEC_HOC_STEP[13] = {
    0, 4915, 6963, 10650, 13107, 18350, 19661, 20972, 20972, 20972, 20972, 20972, 20972,
};

/* x86 variable arithmetic shift of an i32 */
int32_t b25_shift_i32_al2b1(int32_t v, int16_t sh)
{
    if (sh >= 0) {
        return (int32_t)((uint32_t)v << ((uint32_t)sh & 0x1f));
    } else if (sh < -0x1e) {
        return v >> 31;
    } else {
        return v >> ((uint32_t)w16_neg(sh) & 0x1f);
    }
}

/* 0x1030b8e0 - install the frame's bit-allocation descriptor and the default AGC configuration */
int32_t b25_init_bit_alloc_and_agc(int16_t mode, int16_t *alloc, int16_t *agc)
{
    b25_init_agc_config(agc);
    if (mode == 0) {
        b25_imbe_bit_alloc(alloc);
    } else {
        b25_ambe_bit_alloc(alloc, 0);
    }
    return 0;
}

/* 0x1031a2f0 - forward real-input FFT of 2^log2n samples */
int32_t b25_rfft_forward(int16_t *buf, int32_t exp_in, int16_t log2n, int16_t stage_flag)
{
    int32_t c = (int32_t)(uint16_t)b25_fft(buf, exp_in, w16_sub(log2n, 1));
    int32_t r = (int32_t)(uint16_t)b25_fft_merge_stage(buf, c, log2n, 0, stage_flag);
    if (stage_flag == 0) {
        buf[1] = 0;
    }
    return r;
}

/* 0x103176b0 - log2-domain harmonic magnitudes to linear magnitudes sharing one block exponent */
void b25_expand_log_mags(int16_t *v, int16_t *exp, uint16_t n)
{
    int16_t cnt = (int16_t)n;
    size_t i;
    int32_t max;
    int32_t scaled;
    int16_t out_exp;
    if (cnt > 0) {
        for (i = 0; i < (size_t)n; i++) {
            int16_t x = v[i];
            v[i] = x >= 0x7800 ? (int16_t)0x77ff : x < -0x77ff ? (int16_t)-0x77ff : x;
        }
    }

    max = INT32_MIN;
    if (cnt > 0) {
        for (i = 0; i < (size_t)n; i++) {
            int32_t p = w32_shl((int32_t)v[i], 16);
            if (max < p) {
                max = p;
            }
        }
    }
    scaled = w32_add(max >> 0xb, 0x10000);
    out_exp = b25_high16_al2b1(scaled);

    if (cnt > 0) {
        for (i = 0; i < (size_t)n; i++) {
            v[i] = (int16_t)b25_pow2_scaled(v[i], 4, out_exp);
        }
    }
    *exp = w16_sub(out_exp, 0xf);
    b25_fill16(v + cnt, 0, w16_sub(0x38, cnt));
}

/* 0x103114d0 - scalar quantise (or dequantise) the five higher-order cepstral coefficients */
void b25_code_higher_order_coeffs(int16_t *bits, int32_t *pos, const uint16_t *nbits, int16_t *params, int encode)
{
    size_t i;
    for (i = 0; i < 5; i++) {
        uint16_t b = nbits[i];
        int32_t prod = w32_mul(w32_mul((int32_t)DEC_HOC_STEP[b], (int32_t)DEC_HOC_SCALE[i]), 2);
        uint16_t levels = ((b & 0x1f) >= 16) ? (uint16_t)0 : (uint16_t)(1u << (b & 0x1f));
        int16_t step = (int16_t)(prod >> 0x11);
        uint16_t idx;
        int16_t centre;
        int32_t recon;

        if (!encode) {
            idx = b25_read_field_i16(bits, pos, (int16_t)b);
        } else {
            int32_t acc = w32_add(w32_shl((int32_t)params[1 + i], 16), w32_shl((int32_t)step, 16) >> 1);
            uint16_t v;
            if (acc < 0) {
                v = 0;
            } else if (b25_high16_al2b1(acc) < step) {
                int32_t a = acc;
                if ((int16_t)b > 0) {
                    uint16_t t;
                    for (t = 0; t < b; t++) {
                        b25_subc(step, &a);
                    }
                }
                v = (uint16_t)a;
            } else {
                v = w16u_sub(levels, 1);
            }
            b25_emit_bits_i16(bits, pos, (int16_t)b, (int16_t)v);
            idx = v;
        }

        centre = b25_high16_al2b1(
            w32_add(w32_sub(w32_shl((int32_t)(int16_t)idx, 17), w32_shl((int32_t)(int16_t)levels, 16)), 0x10000));
        recon = w32_mul(w32_mul((int32_t)centre, (int32_t)step), 2);
        params[1 + i] = b25_high16_al2b1(b25_shift_i32_al2b1(recon, (int16_t)(0xe - (int32_t)b)));
    }
}

/* read_field_l1_batch4 over an i16 bit array addressed by a word cursor */
uint16_t b25_read_field_i16(const int16_t *bits, int32_t *pos, int16_t n)
{
    uint16_t r = b25_pack_bits_i16(bits + (size_t)*pos, n);
    *pos = w32_add(*pos, (int32_t)n);
    return r;
}

/* emit_bits over an i16 bit array with a word cursor */
void b25_emit_bits_i16(int16_t *bits, int32_t *pos, int16_t n, int16_t word)
{
    size_t c = (size_t)*pos;
    b25_emit_bits(&c, bits, n, word);
    *pos = (int32_t)c;
}

uint32_t b25_rd32_al2b1(const int16_t *b, size_t w)
{
    return (uint32_t)(uint16_t)b[w] | ((uint32_t)(uint16_t)b[w + 1] << 16);
}

void b25_wr32_al2b1(int16_t *b, size_t w, uint32_t v)
{
    b[w] = (int16_t)(uint16_t)v;
    b[w + 1] = (int16_t)(uint16_t)(v >> 16);
}

/* 0x103153a0 - the band voicing decision */
void b25_voicing_decision(int16_t *cur, int16_t *prev, const int16_t *st, uint16_t *mask_io, int16_t thresh, int16_t frames, const int16_t *cfg, int16_t mant, int16_t exp)
{
    uint32_t cur_pat;
    int16_t w0, l, slope, level, triads, nbands;
    int32_t floor_;
    uint32_t bit, mask;
    uint16_t k;
    uint16_t prev_dibits;
    uint32_t pat, o0, o1;

    if (thresh <= 0x28f && frames < 4) {
        return;
    }
    cur_pat = b25_rd32_al2b1(cur, 4);
    if ((cur_pat & 0xaaaaaaaau) != 0) {
        return;
    }
    if ((b25_rd32_al2b1(prev, 4) & 0xaaaaaaaau) != 0) {
        return;
    }
    w0 = cur[6];
    if (!(st[0x44] > 0 || cur_pat != 0 || w0 > 0x40b5 || w0 < 0x3d63)) {
        return;
    }

    l = cur[2];
    slope = (int16_t)b25_weighted_slope(cur + 8, l);
    level = (int16_t)b25_voicing_level(mant, exp, thresh, cfg);
    floor_ = w32_shl((int32_t)(int16_t)b25_log2_q11(w0), 16) >> 2;
    triads = b25_triads_capped(l);

    bit = 1;
    mask = (uint32_t)*mask_io;

    k = (uint16_t)w16_sub(triads, 1);
    if ((int16_t)k >= 0) {
        int32_t lo = w32_mul((int32_t)(int16_t)k, 3);
        uint32_t top = (uint32_t)w16u_mul(k, 3);
        uint32_t hi = 0x38;
        for (;;) {
            uint32_t prev_top = top;
            int16_t peak = -0x5000;
            uint32_t last = hi - 1u;
            uint16_t c = (uint16_t)last;
            int16_t s;
            while ((int32_t)(int16_t)c >= lo) {
                int16_t v = cur[8 + (size_t)(int32_t)(int16_t)c];
                if (peak < v) {
                    peak = v;
                }
                c = w16u_sub(c, 1);
            }
            s = (int16_t)(w32_sub(w32_shl((int32_t)peak, 16), floor_) >> 16);
            if (((int32_t)(int16_t)(uint16_t)hi * 2) > (int32_t)l && slope < 0) {
                s = (int16_t)(w32_add(w32_shl((int32_t)slope, 16) >> 2, w32_shl((int32_t)s, 16)) >> 16);
            }

            if (s > level && ((uint16_t)bit & (uint16_t)mask) == 0) {
                uint16_t j;
                mask |= bit;
                j = (uint16_t)last;
                while ((int32_t)(int16_t)j >= lo) {
                    size_t idx = 8 + (size_t)(int32_t)(int16_t)j;
                    int32_t a = w32_shl((int32_t)cur[idx], 16);
                    int32_t d = w32_sub(a, floor_);
                    int32_t r = (((d ^ a) & (~floor_ ^ d)) < 0) ? w32_add(0x7fffffff, (int32_t)(a < 0)) : d;
                    int16_t v = b25_high16_al2b1(r);
                    cur[idx] = v > 0x77ff ? (int16_t)0x77ff : v < -0x77ff ? (int16_t)-0x77ff : v;
                    j = w16u_sub(j, 1);
                }
            }

            lo = w32_sub(lo, 3);
            bit = w32u_shl(bit, 1);
            k = w16u_sub(k, 1);
            hi = (uint32_t)(uint16_t)prev_top;
            top = prev_top - 3u;
            if ((int16_t)k < 0) {
                break;
            }
        }
    }

    *mask_io = (uint16_t)mask;

    nbands = triads;
    if (nbands > 8) {
        mask = (uint32_t)(uint16_t)b25_map_voicing_bits((int16_t)mask, (int32_t)nbands, l);
        nbands = 8;
    }
    prev_dibits = (uint16_t)b25_gather_dibits((int32_t)b25_rd32_al2b1(st, 4));
    pat = b25_voicing_pattern((int16_t)w16u_add((uint16_t)mask, 0x38), (uint32_t)prev_dibits, nbands);
    o0 = b25_rd32_al2b1(prev, 4);
    o1 = b25_rd32_al2b1(cur, 4);
    b25_expand_dibits(&o0, &o1, pat);
    b25_wr32_al2b1(prev, 4, o0);
    b25_wr32_al2b1(cur, 4, o1);
}

/* one 0x10312f30 copy step with word indices */
int32_t b25_copy_seg(int16_t *chan, int32_t o, int16_t *scratch, int32_t s, int16_t n, int store)
{
    b25_copy16_batch4(chan + (size_t)o, scratch + (size_t)s, n, store);
    if (n > 0) {
        return w32_add(o, (int32_t)n);
    } else {
        return o;
    }
}

/* 0x10312fe0 - interleave (or de-interleave) an IMBE 88-bit frame */
void b25_interleave_imbe_frame(int16_t *chan, int16_t nharm, const int16_t *hbits, const int16_t *alloc, int store)
{
    int16_t scratch[192] = {0};
    int16_t total = alloc[2];
    int16_t f5 = alloc[5];
    int16_t f6 = alloc[6];
    int32_t o, tail, end2;
    int16_t limit, off, triads, cnt;
    ptrdiff_t base, dpos, spos;

    if (!store) {
        b25_copy16_batch3(scratch, chan, total);
    }

    o = b25_copy_seg(chan, 0, scratch, 0, 6, store);
    tail = (int32_t)w16_sub(w16_sub(total, f6), 1);
    o = b25_copy_seg(chan, o, scratch, tail, 3, store);

    limit = w16_sub(nharm, 1);
    off = 0;
    if (limit > 0) {
        size_t i;
        for (i = 0; i < (size_t)(uint16_t)limit; i++) {
            int16_t h = hbits[i];
            if (off < h) {
                off = h;
            }
        }
    }
    triads = b25_triads_capped(nharm);
    cnt = 1;
    base = (ptrdiff_t)w32_add((int32_t)triads, (int32_t)f5);
    dpos = base;
    spos = 0;

    if (o < 0x30) {
        size_t op = (size_t)o;
        off = b25_interleave_walk(chan, &op, 0x30, scratch, &dpos, hbits, &spos, &cnt, base, off, limit, store);
        o = (int32_t)op;
    }

    o = b25_copy_seg(chan, o, scratch, (int32_t)f5, triads, store);
    o = b25_copy_seg(chan, o, scratch, w32_add(tail, 3), 2, store);

    end2 = w32_sub((int32_t)total, 4);
    if (o < end2) {
        size_t op = (size_t)o;
        b25_interleave_walk(chan, &op, (size_t)end2, scratch, &dpos, hbits, &spos, &cnt, base, off, limit, store);
        o = (int32_t)op;
    }

    o = b25_copy_seg(chan, o, scratch, w32_add(tail, 5), 1, store);
    o = b25_copy_seg(chan, o, scratch, 6, 2, store);
    b25_copy_seg(chan, o, scratch, w32_sub((int32_t)total, 1), 1, store);

    if (store) {
        b25_copy16_batch3(chan, scratch, total);
    }
}

/* ======================= from aL2b2 ======================= */

/* shift32_al2b2 with the count compared as a full 32-bit quantity */
int32_t b25_shift32_wide(int32_t v, int32_t s)
{
    if (s < 0) {
        if (s < -0x1e) {
            return v >> 31;
        } else {
            return v >> ((uint32_t)w32_neg(s) & 0x1f);
        }
    } else {
        return (int32_t)((uint32_t)v << ((uint32_t)s & 0x1f));
    }
}

/* x86 shl ax, cl */
uint16_t b25_shl16_al2b2(uint16_t v, uint32_t count)
{
    uint32_t c = count & 0x1f;
    if (c >= 16) {
        return 0;
    } else {
        return (uint16_t)((uint32_t)v << c);
    }
}

/* inlined hardware bit-scan normalisation */
int32_t b25_norm_bsr(int32_t x)
{
    uint32_t mag;
    int32_t b;
    if (x == 0) {
        return 0;
    }
    mag = x < 0 ? ~(uint32_t)x : (uint32_t)x;
    b = mag == 0 ? 0 : 31 - (int32_t)lz32(mag);
    return (int32_t)(uint16_t)(0x1e - b);
}

/* little-endian 32-bit read of two adjacent i16 words */
int32_t b25_rd32_al2b2(const int16_t *v, size_t word)
{
    return (int32_t)(uint16_t)v[word] | w32_shl((int32_t)v[word + 1], 16);
}

/* 0x1030c970 - force a decoded parameter block to a pure signalling tone */
void b25_tone_frame_fixup(int16_t *p, const int16_t *cos_tbl)
{
    int16_t code = p[1];
    int32_t class_ = (int32_t)b25_frame_size_class(code);
    int16_t ia = (int16_t)(uint16_t)b25_band_count_for_mode(class_, code, 0, cos_tbl);
    int16_t ib = (int16_t)(uint16_t)b25_band_count_for_mode(class_, code, 1, cos_tbl);
    int16_t n = p[2];
    int16_t i = 0;
    while (i < n) {
        if (i != ia && i != ib) {
            int32_t x = (int32_t)p[8 + i];
            p[8 + i] = (int16_t)(w32_mul(x, 0xa3c) >> 16);
        }
        i += 1;
    }

    if (p[1] >= 0x80 && p[1] < 0xa0) {
        int32_t a = (int32_t)p[8 + ia];
        int32_t b = (int32_t)p[8 + ib];
        int32_t m = w32_add(w32_mul(b, b), w32_mul(a, a));
        int16_t v = (int16_t)b25_sqrt_to_exp(m, 8, 4);
        p[8 + ib] = v;
        p[8 + ia] = v;
    }

    code = p[1];
    if (code >= 0x14 && code <= 0x16) {
        int16_t den = w16_mul(w16_add(p[3], 1), 0x100);
        p[6] = (int16_t)b25_div32_half(w32_shl((int32_t)code, 0x14), den);
    }
}

/* 0x103165e0 - decide whether the frame just decoded is a signalling tone */
void b25_detect_tone_frame(int16_t *p, int16_t prev_code, const int16_t *hist, const int16_t *cos_tbl, const uint16_t *codewords)
{
    uint16_t n;
    int16_t lin[56] = {0};
    int16_t blk_exp = 0;
    int16_t mean = 0;
    int32_t sum_exp, blk;
    int16_t peak, peak_i, i;
    int16_t dist, code;
    int16_t leg_a, leg_b;
    int32_t e_a, e_b, energy, norm, e_exp;
    int16_t mant, sel, floor_;
    int32_t lim, cap;
    int below;

    p[1] = 0xff;

    n = (uint16_t)p[2];
    b25_pow2_vector(lin, &blk_exp, p + 8, n);

    sum_exp = (int32_t)(uint16_t)b25_sum_norm(&mean, lin, (int32_t)blk_exp, n);
    blk = (int32_t)blk_exp;

    peak = lin[0];
    peak_i = 0;
    i = 1;
    while (i < (int16_t)n) {
        if (peak < lin[i]) {
            peak = lin[i];
            peak_i = i;
        }
        i += 1;
    }

    if (b25_voiced_dibit(p, peak_i) == 1 && peak_i <= 10) {
        int16_t thresh;
        int32_t scaled, cmp;
        if (peak_i <= 0) {
            thresh = 0x7fdf;
        } else {
            int16_t t = 0x7eb8;
            if (peak_i <= 1 && peak_i == (int16_t)b25_band_count_for_mode(0, prev_code, 0, cos_tbl)) {
                t = 0x7f5c;
            }
            thresh = t;
        }

        scaled = b25_shift32_al2b2(w32_shl((int32_t)peak, 16), (int16_t)w32_sub(blk, sum_exp));
        cmp = w32_sub(w32_mul(w32_mul((int32_t)thresh, (int32_t)mean), 2), scaled);
        if (cmp < 0) {
            int16_t forced = 0;
            int32_t lo, hi;
            int matched = 0;
            int accept;
            if (peak_i == 1 && w16u_sub((uint16_t)prev_code, 0x14) < 3) {
                int16_t d = hist[2];
                int16_t k = 0;
                while (k < 2) {
                    if (hist[k] != d) {
                        matched = 1;
                        break;
                    }
                    k += 1;
                }
            }
            if (matched) {
                forced = 1;
                lo = 0x14;
                hi = 0x16;
            } else {
                int32_t c = w32_mul((int32_t)peak_i, 0xccc00);
                lo = (int32_t)(uint16_t)w32_add(c >> 16, 1);
                hi = (int32_t)(uint16_t)(w32_add(c, 0xccc00) >> 16);
            }

            dist = 0x7fff;
            code = (int16_t)lo;
            if ((uint16_t)prev_code < 0x80) {
                b25_nearest_codeword((uint16_t)hist[2], &dist, &code, prev_code, prev_code, codewords);
            }
            b25_nearest_codeword((uint16_t)hist[2], &dist, &code, (int16_t)lo, (int16_t)hi, codewords);

            if (dist <= 1 || (dist <= 2 && code == prev_code)) {
                accept = 1;
            } else if (dist > 0x10) {
                return;
            } else {
                accept = forced != 0;
            }
            if (accept) {
                p[1] = code;
                p[0] = 3;
                p[3] = (int16_t)(uint16_t)b25_band_count_for_mode(0, code, 0, cos_tbl);
            }
            return;
        }
    }

    /* dual-tone arm */
    dist = 0x7fff;
    code = 0x80;
    if (prev_code >= 0x80 && prev_code <= 0xa3) {
        b25_nearest_codeword((uint16_t)hist[2], &dist, &code, prev_code, prev_code, codewords);
    }
    b25_nearest_codeword((uint16_t)hist[2], &dist, &code, 0x80, 0xa3, codewords);

    if (dist > 1 && (dist > 2 || code != prev_code)) {
        return;
    }

    leg_a = (int16_t)(uint16_t)b25_band_count_for_mode(1, code, 0, cos_tbl);
    e_a = b25_voiced_dibit(p, leg_a) == 1 ? w32_shl((int32_t)lin[leg_a], 16) >> 1 : 0;
    leg_b = (int16_t)(uint16_t)b25_band_count_for_mode(1, code, 1, cos_tbl);
    e_b = b25_voiced_dibit(p, leg_b) == 1 ? w32_shl((int32_t)lin[leg_b], 16) >> 1 : 0;

    energy = w32_add(e_a, e_b);
    norm = b25_norm_bsr(energy);
    e_exp = (int32_t)(uint16_t)w32_add(w32_sub(blk, norm), 1);
    mant = (int16_t)((int32_t)((uint32_t)energy << ((uint32_t)norm & 0x1f)) >> 16);

    if (code < 0x90) {
        sel = 0x3200;
    } else if (code < 0xa0) {
        sel = 0x3600;
    } else {
        sel = 0x7800;
    }
    lim = w32_shl((int32_t)sel, 16) >> 10;
    cap = w32_mul((int32_t)p[2], 0xe666);
    if (lim > cap) {
        lim = cap;
    }
    floor_ = b25_high16_al2b1(w32_shl(lim, 10));

    if (w32_add((int32_t)(int16_t)e_exp, 0xf) > w32_add((int32_t)(int16_t)sum_exp, 5)) {
        int32_t d = w32_mul(w32_mul((int32_t)floor_, (int32_t)mean), 2);
        int16_t cnt = (int16_t)w32_sub(w32_sub(sum_exp, e_exp), 10);
        int32_t a = w32_mul(w32_mul((int32_t)mant, (int32_t)p[2]), 2);
        below = w32_sub(a, b25_shift32_al2b2(d, cnt)) < 0;
    } else {
        int32_t a = w32_mul(w32_mul((int32_t)mant, (int32_t)p[2]), 2);
        int16_t cnt = (int16_t)w32_add(w32_sub(e_exp, sum_exp), 10);
        int32_t d = w32_mul(w32_mul((int32_t)floor_, (int32_t)mean), 2);
        below = w32_sub(b25_shift32_al2b2(a, cnt), d) < 0;
    }

    if (!below) {
        p[1] = code;
        p[0] = 3;
        p[3] = (int16_t)(w32_shl((int32_t)leg_a, 8) | (int32_t)(uint16_t)leg_b);
    }
}

/* the voicing dibit of harmonic k */
uint8_t b25_voiced_dibit(const int16_t *p, int16_t k)
{
    int32_t word = b25_rd32_al2b2(p, 4);
    int32_t t = w32_add(w32_mul(w32_mul((int32_t)(int16_t)w32_add((int32_t)k, 1), (int32_t)p[6]), 4), 0x8000) >> 0x11;
    t = (int32_t)(int16_t)t;
    return (uint8_t)((uint8_t)b25_shift32_wide(word, w32_mul(t, -4)) & 3);
}

/* 0x103116b0 - uniform scalar quantiser / dequantiser for the higher-order spectral coefficients */
void b25_quantize_coeff_blocks(int16_t *payload, int32_t *pos, int16_t blocks, uint16_t per_block, int16_t *vals, const uint16_t *allocs, int16_t encode, const int16_t *weight, const int16_t *loading)
{
    int16_t left = w16_sub(blocks, 1);
    size_t vi = 0;
    size_t ai = 0;
    while (left >= 0) {
        left = w16_sub(left, 1);
        vi += 1;
        if ((int16_t)per_block > 0) {
            size_t j = 0;
            uint16_t rem = per_block;
            for (;;) {
                uint16_t bits = allocs[ai];
                ai += 1;
                if ((int16_t)bits < 1) {
                    vals[vi] = 0;
                } else {
                    int32_t prod = w32_mul(w32_mul((int32_t)loading[bits], (int32_t)weight[j]), 2);
                    uint16_t cells = b25_shl16_al2b2(1, (uint32_t)bits);
                    int16_t step = (int16_t)(prod >> 0x11);
                    uint16_t idx;
                    int32_t centre, acc;

                    if (encode == 0) {
                        idx = b25_read_field_al2b2(payload, pos, (int16_t)bits);
                    } else {
                        int32_t acc2 = w32_add(w32_shl((int32_t)step, 16) >> 1, w32_mul((int32_t)vals[vi], 0x10000));
                        uint16_t v;
                        size_t cur;
                        if (acc2 < 0) {
                            v = 0;
                        } else if (b25_high16_al2b1(acc2) < step) {
                            int32_t a = acc2;
                            uint16_t t;
                            for (t = 0; t < bits; t++) {
                                b25_subc(step, &a);
                            }
                            v = (uint16_t)a;
                        } else {
                            v = w16u_sub(cells, 1);
                        }
                        cur = (size_t)*pos;
                        b25_emit_bits(&cur, payload, (int16_t)bits, (int16_t)v);
                        *pos = (int32_t)cur;
                        idx = v;
                    }

                    centre = w32_add(w32_add(w32_mul((int32_t)(int16_t)idx, 0x20000), w32_mul((int32_t)(int16_t)cells, -0x10000)), 0x10000);
                    acc = w32_mul(w32_mul((int32_t)b25_high16_al2b1(centre), (int32_t)step), 2);
                    vals[vi] = b25_high16_al2b1(b25_shift32_al2b2(acc, (int16_t)w16u_sub(0x0e, bits)));
                }
                j += 1;
                vi += 1;
                rem -= 1;
                if (rem == 0) {
                    break;
                }
            }
        }
    }
}

/* 0x103102e0 in its i16-payload form */
uint16_t b25_read_field_al2b2(const int16_t *payload, int32_t *pos, int16_t n)
{
    size_t at = (size_t)*pos;
    uint16_t v = b25_pack_bits_i16(payload + at, n);
    *pos = w32_add(*pos, (int32_t)n);
    return v;
}

/* 0x103132b0 - split one joint frequency code between two voiced sub-bands */
int32_t b25_split_voiced_freq_code(int16_t *code, int16_t *a, int16_t *b, int16_t freq_a, int16_t freq_b, uint32_t bits, int32_t enable)
{
    int32_t map_a = b25_rd32_al2b2(a, 4);
    int32_t map_b = b25_rd32_al2b2(b, 4);
    int a_unvoiced = ((uint32_t)map_a & 0x55555555u) != 0;
    int b_unvoiced = ((uint32_t)map_b & 0x55555555u) != 0;
    int a_voiced = ((uint32_t)map_a & 0xaaaaaaaau) != 0;
    int b_voiced = ((uint32_t)map_b & 0xaaaaaaaau) != 0;
    int on = (int16_t)enable != 0;
    int ok = on ? b[0] == 1 : 1;
    int16_t cap;
    uint16_t n;
    int16_t ni, q, r;

    if (a_unvoiced || b_unvoiced) {
        b[7] = 0;
        a[7] = 0;
        return 0;
    }
    if (!ok || (!a_voiced && !b_voiced)) {
        b[7] = 0;
        a[7] = 0;
        return 0;
    }

    cap = on ? w16_sub((int16_t)b25_shl16_al2b2(0x3c, bits - 6u), 1) : (int16_t)0;

    if (!a_voiced) {
        if (b_voiced) {
            int16_t nn;
            a[7] = 0;
            nn = (int16_t)b25_shl16_al2b2(1, bits);
            b[7] = (int16_t)b25_harmonic_index(code, nn, freq_b, cap, (int16_t)enable);
        }
        return 1;
    }
    if (!b_voiced) {
        int16_t nn;
        b[7] = 0;
        nn = (int16_t)b25_shl16_al2b2(1, bits);
        a[7] = (int16_t)b25_harmonic_index(code, nn, freq_a, cap, (int16_t)enable);
        return 1;
    }

    n = b25_shl16_al2b2(1, (uint32_t)((int32_t)bits >> 1));
    if ((bits & 1) != 0) {
        n = (uint16_t)b25_high16_al2b1(w32_mul((int32_t)(int16_t)n, 0x16a08));
    }
    ni = (int16_t)n;

    q = 0;
    r = 0;
    if (!on) {
        int16_t quo = (int16_t)b25_div32((int32_t)*code, ni);
        int16_t rem = w16_sub(*code, w16_mul(quo, ni));
        q = quo;
        r = rem;
        if ((int32_t)quo > w32_sub((int32_t)ni, 1)) {
            q = (int16_t)w16u_sub(n, 1);
        }
        if ((int32_t)rem > w32_sub((int32_t)ni, 1)) {
            r = (int16_t)w16u_sub(n, 1);
        }
    }

    a[7] = (int16_t)b25_harmonic_index(&r, ni, freq_a, cap, (int16_t)enable);
    b[7] = (int16_t)b25_harmonic_index(&q, ni, freq_b, cap, (int16_t)enable);

    if (on) {
        int16_t v = w16_add(r, w16_mul(q, ni));
        *code = v;
        if (cap < v) {
            *code = cap;
        } else if (v < 1) {
            *code = 0;
        }
    }
    return 1;
}

/* spectral slope via spectrum history and Hilbert differentiator */
void b25_spectral_slope(int16_t *out, const int16_t *src, int16_t n, const int16_t *taps)
{
    int16_t scratch[0x68] = {0};
    b25_build_spectrum_history(scratch, src, n);
    b25_hilbert_diff(out, scratch, (uint16_t)n, taps);
}

/* inverse real FFT: merge stage then complex FFT */
int32_t b25_inverse_real_fft(int16_t *buf, int32_t exp, int32_t log2n, int16_t first)
{
    int32_t e;
    if (first == 0) {
        buf[1] = 0;
    }
    e = (int32_t)(uint16_t)b25_fft_merge_stage(buf, exp, (int16_t)log2n, 1, first);
    return w32_add(w32_sub(b25_fft(buf, e, (int16_t)w32_sub(log2n, 1)), log2n), 1);
}

/* ======================= from aL2b3 ======================= */

int64_t b25_allshl_al2b3(int64_t v, uint8_t count)
{
    uint32_t hi = (uint32_t)((uint64_t)v >> 32);
    uint32_t lo = (uint32_t)v;
    uint32_t n, new_hi, new_lo;
    if (count >= 0x40) {
        return 0;
    }
    if (count >= 0x20) {
        return (int64_t)((uint64_t)(lo << (count & 0x1f)) << 32);
    }
    n = (uint32_t)(count & 0x1f);
    if (n == 0) {
        new_hi = hi;
        new_lo = lo;
    } else {
        new_hi = (hi << n) | (lo >> (32 - n));
        new_lo = lo << n;
    }
    return (int64_t)(((uint64_t)new_hi << 32) | (uint64_t)new_lo);
}

int32_t b25_sat_sub32(int32_t a, int32_t b)
{
    int32_t r = w32_sub(a, b);
    if (((~b ^ r) & (r ^ a)) < 0) {
        return w32_add(0x7fffffff, (int32_t)(a < 0));
    } else {
        return r;
    }
}

uint32_t b25_mask32(const int16_t *rec)
{
    return (uint32_t)(uint16_t)rec[4] | ((uint32_t)(uint16_t)rec[5] << 16);
}

void b25_retune_from_pitch_msg(int16_t *state, int16_t *flags, const int16_t *msg)
{
    int16_t idx, d;
    int32_t num;
    uint32_t e;
    if (b25_frame_size_class(state[1]) != 0) {
        return;
    }
    if (msg[0] != 3) {
        return;
    }
    if (b25_frame_size_class(msg[1]) != 0) {
        return;
    }

    idx = msg[3];
    d = w16_sub(state[3], idx);
    d = d < 0 ? w16_neg(d) : d;
    if (d <= 1 && (int32_t)state[1] < w32_mul(w32_add((int32_t)idx, 1), 0x10)) {
        b25_select_harmonic(state, flags, idx);
        return;
    }

    num = w32_add(w32_mul(w32_add((int32_t)idx, 1), (int32_t)state[1]), (int32_t)msg[1] / 2);
    e = (uint32_t)(uint16_t)b25_div32(num, msg[1]);
    while (w32_mul((int32_t)(int16_t)(uint16_t)e, 0x10) <= (int32_t)state[1]) {
        e = e + 1u;
    }
    b25_select_harmonic(state, flags, (int16_t)(uint16_t)(e - 1u));
}

int32_t b25_block_dft_dc(int16_t *dst, int16_t *src, uint16_t count, uint32_t n, const int16_t *cos_tbl)
{
    int32_t off = 0;
    if ((int16_t)count > 0) {
        size_t k;
        for (k = 0; k < (size_t)count; k++) {
            b25_harmonic_dft(src + (size_t)off, (uint16_t)n, (int16_t)n, cos_tbl);
            dst[k] = src[(size_t)off];
            off = w32_add(off, (int32_t)(int16_t)(uint16_t)n);
        }
    }
    return off;
}

void b25_harmonic_idct(int16_t *buf, int16_t n, int16_t count, const int16_t *cos_tbl)
{
    int16_t cnt = count >= n ? n : count;
    int16_t local[56] = {0};
    int16_t recip, step;
    const int16_t *ct;
    const int16_t *src;
    size_t src_len, s, c;

    local[0] = buf[0];
    if (cnt > 1) {
        b25_shift_copy16(local + 1, buf + 1, (uint16_t)(cnt - 1), 1);
    }
    if (n <= 0) {
        return;
    }

    recip = DSP_RECIP_N[n];
    step = (int16_t)(recip >> 1);
    ct = cos_tbl;
    src = local;
    src_len = cnt > 0 ? (size_t)cnt : 0;
    for (s = 0; s < (size_t)n; s++) {
        int32_t acc = 0;
        int32_t phase = 0x40;
        for (c = 0; c < src_len; c++) {
            size_t ti = (size_t)((phase >> 7) & 0x1ff);
            phase = w32_add(phase, (int32_t)step);
            acc = w32_add(acc, w32_mul(w32_mul((int32_t)ct[ti], (int32_t)src[c]), 2));
        }
        step = w16_add(step, recip);
        buf[s] = b25_high16_al2b3(w32_add(acc, 0x8000));
    }
}

uint16_t b25_merge_voiced_amplitude(uint32_t flags, const int16_t *a, const int16_t *b)
{
    if ((b25_mask32(b) & 0x55555555u) != 0 && (flags & 0xaaaaaaaau) == 0) {
        if ((b25_mask32(a) & 0x55555555u) != 0) {
            int32_t p = w32_mul(w32_mul((int32_t)a[6], (int32_t)b[6]), 2);
            return (uint16_t)b25_sqrt_to_exp(p, -8, -4);
        }
        return (uint16_t)b[6];
    }
    return (uint16_t)a[6];
}

void b25_attenuate_unvoiced_bands(int16_t *out, const int16_t *inp, int16_t mask, int16_t level, int16_t nbits, int16_t negate)
{
    int32_t l = b25_log2_q(level, 4);
    int16_t off = b25_high16_al2b3(w32_sub(w32_shl(l, 10), 0x0314fc01));
    int16_t bands, m, idx;
    int32_t lo, cur;
    if (negate != 0) {
        off = w16_neg(off);
    }

    bands = w16_sub(b25_triads_capped(nbits), 1);
    if (bands < 0) {
        return;
    }

    m = mask;
    lo = (int32_t)bands * 3;
    idx = 0x37;
    cur = 0x37;
    for (;;) {
        int voiced = (m & 1) != 0;
        m = (int16_t)(m >> 1);
        while (lo <= cur) {
            int16_t v = inp[(size_t)cur];
            if (!voiced) {
                int32_t r = b25_sat_sub32(w32_shl((int32_t)v, 16), w32_shl((int32_t)off, 16));
                v = b25_high16_al2b3(r);
                if (v > 0x77ff) {
                    v = 0x77ff;
                } else if (v < -0x7800) {
                    v = -0x7800;
                }
            }
            idx = w16_sub(idx, 1);
            out[(size_t)cur] = v;
            cur = (int32_t)idx;
        }
        bands = w16_sub(bands, 1);
        lo -= 3;
        if (bands < 0) {
            return;
        }
    }
}

void b25_harmonic_energy_and_corr(int16_t *out_energy, int16_t *out_energy_exp, int16_t *out_corr, int16_t *out_corr_exp, int16_t *cos_out, int16_t omega, const int16_t *amps, int16_t exp_adj, uint16_t n, const int16_t *cos_tbl)
{
    int16_t e = 0;
    int32_t energy = b25_dot_norm64(&e, amps, amps, n);
    int16_t adj, sh, m;
    int32_t step, phase;
    int64_t acc, shifted;
    *out_energy = b25_high16_al2b3(energy);
    adj = w16_mul(exp_adj, 2);
    *out_energy_exp = w16_add(e, adj);

    if (*out_energy == 0) {
        *out_corr = 0;
        *out_corr_exp = 0;
        return;
    }

    step = w32_shl((int32_t)omega, 10) >> 4;
    phase = step;
    acc = 0;
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t ti = (int32_t)b25_high16_al2b3(phase);
            int16_t c = cos_tbl[(size_t)ti];
            int16_t sq;
            cos_out[i] = c;
            sq = b25_high16_al2b3(w32_mul(w32_mul((int32_t)amps[i], (int32_t)amps[i]), 2));
            acc = w64_add(acc, (int64_t)w32_mul(w32_mul((int32_t)sq, (int32_t)c), 2));
            phase = w32_add(phase, step);
        }
    }

    sh = (int16_t)(uint16_t)b25_norm64((uint32_t)acc, (uint32_t)(acc >> 32));
    if (sh >= 0) {
        shifted = b25_allshl_al2b3(acc, (uint8_t)((uint32_t)sh & 0xff));
    } else {
        shifted = b25_allshr(acc, (uint8_t)((uint32_t)w32_neg((int32_t)sh) & 0xff));
    }
    *out_corr_exp = w16_sub(adj, sh);
    m = b25_high16_al2b3((int32_t)shifted);
    *out_corr = m == -0x8000 ? (int16_t)-0x7fff : m;
}

/* ======================= from aL2b4 ======================= */

int32_t b25_bsr(uint32_t v)
{
    return 31 - (int32_t)lz32(v);
}

int32_t b25_up16_al2b4(int16_t x)
{
    return (int32_t)((uint32_t)x << 16);
}

int32_t b25_add_sat_sgn_al2b4(int32_t a, int32_t b, int32_t sgn)
{
    int32_t r = w32_add(a, b);
    if (((r ^ a) & (r ^ b)) < 0) {
        if (sgn < 0) {
            return INT32_MIN;
        } else {
            return INT32_MAX;
        }
    } else {
        return r;
    }
}

int16_t b25_minus_1p25(int16_t x)
{
    int32_t a = b25_up16_al2b4(x);
    int32_t r = w32_add(a, (int32_t)0xb0000000u);
    if (((r ^ a) & ~r) < 0) {
        r = a < 0 ? INT32_MIN : INT32_MAX;
    }
    return b25_high16_al2b1(r);
}

static int16_t dec_clamp_l(int16_t v, int16_t top)
{
    if (v > top) {
        return top;
    } else if (v < -0x5000) {
        return -0x5000;
    } else {
        return v;
    }
}

uint32_t b25_pitch_config_init(int16_t *cfg, int16_t rate, int16_t lo, int16_t hi, int16_t apply, const int16_t *tbl)
{
    uint16_t u = (uint16_t)rate;
    int16_t cls;
    int16_t b_lo, b_hi, l, v_lo, v_hi, p_lo, p_hi;
    size_t k;

    if (w16u_sub(u, 5) < 0x76) {
        cls = 0;
    } else if (w16u_sub(u, 0x80) < 0x10) {
        cls = 1;
    } else if (w16u_sub(u, 0x90) < 0x10) {
        cls = 2;
    } else if (w16u_sub(u, 0xa0) < 4) {
        cls = 3;
    } else {
        return 4;
    }

    cfg[0] = 3;
    cfg[4] = 1;
    cfg[5] = 0;
    cfg[7] = 0;
    b25_fill16(cfg + 8, -0x4fff, 0x38);

    if (cls == 0) {
        int16_t band = (int16_t)(uint16_t)b25_band_count_for_mode(0, rate, 0, tbl);
        int32_t num;
        int16_t den, peak;
        cfg[1] = rate;
        cfg[3] = band;

        num = (int32_t)w32u_shl((uint32_t)(int32_t)rate, 20);
        den = b25_high16_al2b1((int32_t)w32u_shl((uint32_t)((int32_t)band + 1), 24));
        cfg[6] = (int16_t)b25_div32_half(num, den);

        l = w16_add(band, 1);
        cfg[2] = l > 0x38 ? (int16_t)0x38 : l < 9 ? (int16_t)9 : l;

        peak = dec_clamp_l(hi, 0x77ff);
        cfg[8 + band] = peak;
        if (apply != 0) {
            b25_spread_peak(cfg + 8, band, b25_minus_1p25(peak), 0x400);
        }
        return 0;
    }

    b_lo = (int16_t)(uint16_t)b25_band_count_for_mode((int32_t)cls, rate, 0, tbl);
    b_hi = (int16_t)(uint16_t)b25_band_count_for_mode((int32_t)cls, rate, 1, tbl);
    cfg[1] = rate;
    k = (size_t)w16_sub(rate, 0x80);
    cfg[3] = (int16_t)(((uint32_t)(uint16_t)b_lo << 8) + (uint32_t)(uint16_t)b_hi);
    cfg[6] = tbl[DEC_T_STEP + k];

    l = w16_add(b_hi, 1);
    cfg[2] = l > 0x38 ? (int16_t)0x38 : l < 9 ? (int16_t)9 : l;

    v_lo = lo;
    v_hi = hi;
    if (apply != 0) {
        int32_t a = b25_up16_al2b4(hi) >> 1;
        int32_t b = b25_up16_al2b4(lo) >> 1;
        int16_t mid = b25_high16_al2b1(b25_add_sat_sgn_al2b4(a, b, b));
        int32_t dev = b25_up16_al2b4(tbl[DEC_T_DEV + k]);
        int32_t mv = b25_up16_al2b4(mid);
        int32_t d = w32_sub(mv, dev);
        if ((((~dev) ^ d) & (d ^ mv)) < 0) {
            d = mv < 0 ? INT32_MIN : INT32_MAX;
        }
        v_lo = b25_high16_al2b1(d);
        v_hi = b25_high16_al2b1(b25_add_sat_sgn_al2b4(dev, mv, mv));
    }

    p_lo = dec_clamp_l(v_lo, 0x6fff);
    cfg[8 + b_lo] = p_lo;
    if (apply != 0) {
        b25_spread_peak(cfg + 8, b_lo, b25_minus_1p25(p_lo), 0x400);
    }

    p_hi = dec_clamp_l(v_hi, 0x6fff);
    cfg[8 + b_hi] = p_hi;
    if (apply != 0) {
        b25_spread_peak(cfg + 8, b_hi, b25_minus_1p25(p_hi), 0x400);
    }
    return 0;
}

int32_t b25_band_pair_mean(const int16_t *arr, int16_t rate, const int16_t *tbl)
{
    int32_t cls = (int32_t)(int16_t)b25_frame_size_class(rate);
    int16_t i_lo = (int16_t)(uint16_t)b25_band_count_for_mode(cls, rate, 0, tbl);
    int16_t i_hi = (int16_t)(uint16_t)b25_band_count_for_mode(cls, rate, 1, tbl);
    int32_t a = b25_up16_al2b4(arr[i_hi]) >> 1;
    int32_t b = b25_up16_al2b4(arr[i_lo]) >> 1;
    return w32_add(a, b) >> 16;
}

void b25_interp_envelope(int16_t *out, int16_t num, int16_t den, const int16_t *src, uint16_t n)
{
    int16_t tmp[60] = {0};
    int32_t step, acc;
    size_t s;
    b25_spread_hold(tmp, src, 0x3c);
    step = b25_ratio_q16(num, den);

    if ((int16_t)n <= 0) {
        return;
    }
    acc = step;
    for (s = 0; s < (size_t)n; s++) {
        int16_t idx = b25_high16_al2b1(acc);
        int16_t w = b25_high16_al2b1((int32_t)((uint32_t)w32_sub(acc, b25_up16_al2b4(idx)) << 15));
        int16_t i0 = idx > 0x3b ? (int16_t)0x3b : idx;
        int16_t i1;
        int32_t lo, hi;
        {
            int16_t t = w16_add(i0, 1);
            i1 = t > 0x3b ? (int16_t)0x3b : t;
        }

        lo = w32_mul(w32_mul((int32_t)tmp[i0], (int32_t)w16_sub(0x7fff, w)), 2);
        if (w == INT16_MIN && tmp[i1] == INT16_MIN) {
            hi = INT32_MAX;
        } else {
            hi = w32_mul(w32_mul((int32_t)tmp[i1], (int32_t)w), 2);
        }

        out[s] = b25_high16_al2b1(b25_add_sat_sgn_al2b4(hi, lo, lo));
        acc = w32_add(acc, step);
    }
}

void b25_rebase_block_exp(int16_t *dst, const int16_t *src, uint16_t n, int32_t exp_src, int32_t exp_dst)
{
    b25_shift_copy16(dst, src, n, (int16_t)w32_sub(exp_src, exp_dst));
}

void b25_vector_rms(int16_t *out_mant, int16_t *out_exp, const int16_t *vec, int16_t n)
{
    int16_t sum_exp = 0;
    int32_t sum = b25_dot_norm64(&sum_exp, vec, vec, (uint16_t)w16_mul(n, 2));
    int16_t m = b25_high16_al2b1(sum);
    int32_t v, den, r;
    uint16_t nrm, exp;
    int16_t q, e;

    if (m <= 0) {
        *out_mant = 0;
        *out_exp = 0;
        return;
    }

    v = b25_up16_al2b4(n);
    if (v == 0) {
        nrm = 0;
    } else {
        uint32_t mag = v < 0 ? ~(uint32_t)v : (uint32_t)v;
        nrm = (uint16_t)(0x1e - b25_bsr(mag));
    }
    den = (int32_t)((uint32_t)v << ((uint32_t)nrm & 0x1f));
    exp = (uint16_t)((0x0fu - (uint32_t)nrm) & 0xffff);
    if (m <= b25_high16_al2b1(den)) {
        den >>= 1;
        exp = w16u_add(exp, 1);
    }

    q = (int16_t)b25_div32_half(den, m);
    e = w16_sub((int16_t)exp, sum_exp);
    r = b25_sqrt_norm((int32_t)w32u_shl((uint32_t)(int32_t)q, 16), &e);
    *out_mant = b25_high16_al2b1(r);
    *out_exp = e;
}

int16_t b25_codebook_field(int16_t *out, uint16_t nwords, int16_t bits_avail, const int16_t *book, int16_t nbits, int16_t stride, int16_t *bits, int32_t *pos, int16_t encode)
{
    int16_t nb = nbits;
    int16_t st = stride;
    int16_t idx;
    if (bits_avail < nbits) {
        uint8_t k = (uint8_t)((uint8_t)((uint8_t)nbits - (uint8_t)bits_avail) & 0x1f);
        int16_t f;
        nb = bits_avail;
        f = k >= 16 ? (int16_t)0 : w16_shl(1, k);
        st = w16_mul(f, stride);
    }

    if (encode != 0) {
        int16_t cand;
        int16_t found;
        size_t cur;
        {
            uint8_t k = (uint8_t)((uint8_t)nb & 0x1f);
            cand = k >= 16 ? (int16_t)0 : w16_shl(1, k);
        }
        found = b25_vq_search(out, book, (int16_t)nwords, cand, st);
        cur = (size_t)*pos;
        b25_emit_bits(&cur, bits, nb, found);
        *pos = (int32_t)cur;
        idx = found;
    } else {
        idx = (int16_t)b25_read_field_i16(bits, pos, nb);
    }

    if ((int16_t)nwords > 0) {
        int32_t base = w32_mul((int32_t)idx, (int32_t)st);
        size_t i;
        for (i = 0; i < (size_t)nwords; i++) {
            out[i] = book[(size_t)w32_add(base, (int32_t)i)];
        }
    }
    return idx;
}

/* ======================= from aL2b5 ======================= */

uint16_t b25_norm_shift_al2b5(int32_t x)
{
    uint32_t m;
    if (x == 0) {
        return 0;
    }
    m = x < 0 ? ~(uint32_t)x : (uint32_t)x;
    return (uint16_t)(0x1e - b25_top_bit_al2b5(m));
}

int32_t b25_shift32_al2b5(int32_t v, int16_t s)
{
    if (s >= 0) {
        return (int32_t)((uint32_t)v << ((uint32_t)s & 0x1f));
    } else if (s < -0x1e) {
        return v >> 31;
    } else {
        return v >> ((uint32_t)(-(int32_t)s) & 0x1f);
    }
}

int32_t b25_sat_add_al2b5(int32_t a, int32_t b, int32_t lim)
{
    int32_t s = w32_add(a, b);
    if (((s ^ a) & (s ^ b)) < 0) {
        return w32_add(0x7fffffff, (int32_t)(lim < 0));
    } else {
        return s;
    }
}

int32_t b25_sat_sub_al2b5(int32_t a, int32_t b, int32_t lim)
{
    int32_t d = w32_sub(a, b);
    if (((a ^ b) & (d ^ a)) < 0) {
        return w32_add(0x7fffffff, (int32_t)(lim < 0));
    } else {
        return d;
    }
}

void b25_fundamental_from_log2(int16_t *w0_out, int16_t *l_out, int16_t log_pitch, int16_t allow_high)
{
    uint8_t cl = 0xfc;
    int16_t x = log_pitch;
    int32_t d, a;
    int16_t v, w0;
    uint32_t c;
    if (log_pitch < -0x1000) {
        uint16_t units = w16u_add((uint16_t)(w16u_sub(0xefff, (uint16_t)log_pitch) >> 12), 1);
        x = w16_add(log_pitch, w16_mul((int16_t)units, 0x1000));
        cl = (uint8_t)w16u_sub(units, 4);
    }
    d = (int32_t)x;
    a = (int32_t)(int16_t)w32_add(w32_mul(d, 0x13b) >> 12, 0x71b);
    a = (int32_t)(int16_t)w32_add(w32_mul(a, d) >> 12, 0x1ec0);
    a = (int32_t)(int16_t)w32_add(w32_mul(a, d) >> 12, 0x58b9);
    v = (int16_t)w32_sub(w32_mul(a, d) >> 12, 0x8000);

    c = (uint32_t)(cl & 0x1f);
    w0 = c >= 16 ? (int16_t)(v >> 15) : (int16_t)(v >> c);

    if (w0 < 0x1079) {
        w0 = 0x1079;
    } else if (w0 > 0x6bca && allow_high == 0) {
        w0 = 0x6bca;
    }
    *w0_out = w0;
    *l_out = b25_harmonic_count(w0);
}

void b25_prba_transform(int16_t *out, int16_t *scratch, const int16_t *b, const int16_t *a, uint16_t n, const uint16_t *lens, const int16_t *cos_tbl)
{
    size_t off, k;
    if ((int16_t)n > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++) {
            int32_t d = (int32_t)a[i] - (int32_t)b[i];
            scratch[i] = d > 0x7fff ? (int16_t)0x7fff : d < -0x7fff ? (int16_t)-0x7fff : (int16_t)d;
        }
    }

    off = 0;
    for (k = 0; k < 4; k++) {
        uint16_t len = lens[k];
        b25_harmonic_dft(scratch + off, len, 6, cos_tbl);
        out[2 * k] = scratch[off];
        out[2 * k + 1] = scratch[off + 1];
        b25_fill16(scratch + off + 6, 0, w16_sub((int16_t)len, 6));
        off = off + (size_t)(int16_t)len;
    }

    for (k = 0; k < 4; k++) {
        int32_t p = w32_mul((int32_t)out[2 * k + 1], 0xb504);
        int32_t q = w32_shl((int32_t)out[2 * k], 16) >> 1;
        out[2 * k] = b25_sat15(w32_add(p, q) >> 15);
        out[2 * k + 1] = b25_sat15(w32_sub(q, p) >> 15);
    }

    b25_harmonic_dft(out, 8, 8, cos_tbl);
}

int16_t b25_sat15(int32_t v)
{
    if (v > 0x7fff) {
        return 0x7fff;
    } else if (v < -0x7fff) {
        return -0x7fff;
    } else {
        return (int16_t)v;
    }
}

void b25_normalise_to_exp(int16_t *dst, const int16_t *src, uint16_t n, int16_t *exp)
{
    uint16_t e = (uint16_t)b25_norm_exp_absmax(src, n);
    b25_shift_copy16(dst, src, n, (int16_t)w16u_sub(0, e));
    *exp = w16_add(*exp, (int16_t)e);
}

void b25_suppress_spectral_peaks(int16_t *cur, int16_t *prev, int16_t frame_kind, int16_t sub_mode)
{
    int16_t l = cur[2];
    int16_t e = 0;
    int32_t energy, ll, den, t, s0, s1;
    uint16_t sh, rms, floor0, n1, n2;
    int16_t mean;
    int16_t curve[60] = {0};
    int32_t tr, base, w1, num, w2, den2;
    int16_t ratio_exp, ratio;
    int32_t floor_q31, ratio_i;
    uint16_t i;

    /* stage 1: RMS and the level tracker */
    energy = b25_dot_norm64(&e, cur + 8, cur + 8, (uint16_t)l);
    e = w16_add(e, 8);

    ll = w32_shl((int32_t)l, 16);
    sh = b25_norm_shift_al2b5(ll);
    den = (int32_t)((uint32_t)ll << ((uint32_t)sh & 0x1f)) >> 16;
    mean = b25_div_norm(energy, e, (int16_t)den, (int16_t)(0xf - (int32_t)sh), &e);
    rms = (uint16_t)b25_sqrt_to_exp(w32_shl((int32_t)mean, 16), e, 4);

    t = (int32_t)prev[0x41];
    s0 = b25_sat_add_al2b5(w32_shl(t, 16), (int32_t)0xf8000000u, w32_shl(t, 16));
    s1 = b25_sat_sub_al2b5(s0, w32_shl((int32_t)(int16_t)rms, 16), s0);
    if (s1 < 0) {
        prev[0x41] = (int16_t)(w32_add(w32_mul(t, 0xfae0), w32_mul((int32_t)(int16_t)rms, 0xa3d6) >> 5) >> 16);
    } else {
        prev[0x41] = (int16_t)(w32_sub(w32_shl(t, 16), 0x00130001) >> 16);
    }

    if (frame_kind < 4 && sub_mode < 2) {
        return;
    }

    /* stage 2: the previous frame's curve and the floor */
    b25_spread_hold_alt(curve, 0x3c, prev + 8);

    tr = (int32_t)prev[0x41];
    base = w32_sub(0x2c000000, w32_shl((int32_t)sub_mode, 27) >> 1);
    floor0 = (uint16_t)(b25_sat_add_al2b5(w32_shl(tr, 16), base, base) >> 16);

    w1 = w32_shl((int32_t)cur[6], 16);
    n1 = b25_norm_shift_al2b5(w1);
    num = (int32_t)((uint32_t)w1 << ((uint32_t)n1 & 0x1f));
    w2 = w32_shl((int32_t)prev[6], 16);
    n2 = b25_norm_shift_al2b5(w2);
    den2 = (int32_t)((uint32_t)w2 << ((uint32_t)n2 & 0x1f)) >> 16;
    ratio_exp = 0;
    ratio = b25_div_norm(num, (int16_t)(uint16_t)(0u - (uint32_t)n1 - 4u), (int16_t)den2,
                         (int16_t)(uint16_t)(0xfffffffcu - (uint32_t)n2), &ratio_exp);

    if (l <= 0) {
        return;
    }

    /* stage 3: per-harmonic clamp */
    floor_q31 = w32_shl((int32_t)(int16_t)floor0, 16);
    ratio_i = (int32_t)ratio;
    i = 0;
    for (;;) {
        int16_t k = (int16_t)w16u_add(i, 1);
        int32_t tilt6 = (int32_t)(int16_t)(w32_mul((int32_t)k, 0x60000) >> 16);
        int32_t tilt = w32_shl(w32_mul(tilt6, (int32_t)cur[6]), 8) >> 16;
        int32_t fl, warped, idx, frac, co, interp, interp_q31, c, d1;
        uint16_t fl16;
        size_t p;
        tilt = (int32_t)(uint16_t)tilt;
        if ((int16_t)(uint16_t)tilt > 0x1000) {
            tilt = 0x1000;
        }
        fl = w32_sub(floor_q31, w32_shl((int32_t)(int16_t)(uint16_t)tilt, 16));
        fl16 = (uint16_t)(fl >> 16);

        warped = b25_shift32_al2b5(w32_mul(w32_mul(ratio_i, (int32_t)k), 2), ratio_exp);
        idx = (int32_t)(int16_t)(uint16_t)(warped >> 16);
        frac = (int32_t)(int16_t)(uint16_t)(w32_shl(w32_sub(warped, w32_shl(idx, 16)), 15) >> 16);
        co = (int32_t)(int16_t)(0x7fff - frac);
        interp = (int32_t)(int16_t)(uint16_t)(w32_mul(w32_add(w32_mul(co, (int32_t)curve[(size_t)idx]),
                                                              w32_mul((int32_t)curve[(size_t)idx + 1], frac)),
                                                      2) >> 16);

        p = 8 + (size_t)(int16_t)i;
        interp_q31 = w32_shl(interp, 16);
        c = (int32_t)cur[p];
        d1 = b25_sat_sub_al2b5(w32_shl(c, 16), interp_q31, w32_shl(c, 16));
        if (b25_sat_add_al2b5(d1, -0x10000000, d1) > 0) {
            int32_t d3;
            c = (int32_t)cur[p];
            d3 = b25_sat_sub_al2b5(w32_shl(c, 16), w32_shl((int32_t)(int16_t)fl16, 16), w32_shl(c, 16));
            if (d3 > 0) {
                int16_t g = (int16_t)(uint16_t)(d3 >> 16);
                int16_t delta, ns;
                int32_t prod, step;
                if (g > 0x400) {
                    g = 0x400;
                }
                c = (int32_t)cur[p];
                delta = (int16_t)(b25_sat_sub_al2b5(interp_q31, w32_shl(c, 16), interp_q31) >> 16);
                if (delta == -0x8000 && g == -0x8000) {
                    prod = 0x7fffffff;
                } else {
                    prod = w32_mul(w32_mul((int32_t)delta, (int32_t)g), 2);
                }
                step = b25_shl_sat(prod, 4);
                c = (int32_t)cur[p];
                ns = (int16_t)(b25_sat_add_al2b5(w32_shl(c, 16), step, step) >> 16);
                cur[p] = ns;
                cur[p] = ns > 0x77ff ? (int16_t)0x77ff : ns < -0x77ff ? (int16_t)-0x77ff : ns;
            }
        }

        i = w16u_add(i, 1);
        if ((int16_t)i >= l) {
            break;
        }
    }
}

void b25_synth_harmonic_span(int32_t *dst, int16_t pos, int16_t pos_exp, int16_t step, const int16_t *table, int32_t exp, const int16_t *cos_tbl)
{
    int16_t start = 0;
    int16_t start_frac = 0;
    int16_t end = 0;
    int16_t end_frac = 0;
    int16_t e;
    uint16_t last, cur, n_rise, n_body, n_fall;
    int32_t fall_phase, rise_phase, stp, phase;
    int16_t shift;
    size_t cursor;

    b25_ceil_and_residual(&start, &start_frac, pos, pos_exp);
    b25_add_recip_split(&end, &end_frac, pos, pos_exp, step);

    e = end;
    if (0xa6 < e) {
        e = 0xa6;
    }
    if ((int32_t)e + 0x10 <= 0xa6) {
        last = w16u_add((uint16_t)e, 0x10);
        if ((int16_t)last <= 0) {
            return;
        }
    } else {
        last = 0xa6;
    }

    fall_phase = w32_add(w32_shl((int32_t)end_frac, 16) >> 15, 0xf0000);
    rise_phase = w32_shl((int32_t)start_frac, 16) >> 15;
    stp = (int32_t)step;
    phase = w32_mul(w32_mul(stp, (int32_t)start_frac), 2) >> 11;

    if (start < 0) {
        phase = w32_add(phase, w32_shl(w32_mul((int32_t)w16_neg(start), stp), 5));
        if (start < -0x10) {
            rise_phase = w32_add(rise_phase, 0x100000);
        } else {
            rise_phase = w32_sub(rise_phase, w32_shl((int32_t)start, 16));
        }
        if (e < 0) {
            fall_phase = w32_add(fall_phase, w32_shl((int32_t)w16_add(e, 1), 16));
        }
        cur = 0;
    } else {
        cur = (uint16_t)start;
    }

    shift = (int16_t)(uint16_t)w32_sub(exp, 0x10);
    cursor = (size_t)(int32_t)(int16_t)cur;

    n_rise = w16u_add(w16u_sub((uint16_t)start, cur), 0x10);
    if ((int16_t)n_rise > 0) {
        cur = w16u_add(cur, n_rise);
        b25_synth_harmonic(&phase, table, step, rise_phase, &cursor, dst, shift, 1, n_rise, cos_tbl);
    }

    n_body = w16u_add(w16u_sub((uint16_t)e, cur), 1);
    if ((int16_t)n_body > 0) {
        cur = w16u_add(cur, n_body);
        b25_synth_harmonic(&phase, table, step, 0, &cursor, dst, shift, 0, n_body, cos_tbl);
    }

    n_fall = w16u_add(w16u_sub(last, cur), 1);
    b25_synth_harmonic(&phase, table, step, fall_phase, &cursor, dst, shift, -1, n_fall, cos_tbl);
}

/* ======================= from aL3b1 ======================= */

int16_t b25_shifted_hi(int32_t v, int16_t sh)
{
    int32_t t;
    if (sh >= 0) {
        t = (int32_t)((uint32_t)v << ((uint32_t)sh & 0x1f));
    } else if (sh < -0x1e) {
        t = v >> 31;
    } else {
        t = v >> ((uint32_t)(-(int32_t)sh) & 0x1f);
    }
    return (int16_t)(t >> 16);
}

void b25_dec_init(int16_t *state, int16_t mode)
{
    size_t i;
    int16_t *lo;
    int16_t *hi;
    b25_fill16(state, 0, 0x3f4);
    state[0x7c2 / 2] = -0x1e;
    state[0x7be / 2] = 0x1c94;
    (void)b25_frame_size_class(0x20);
    state[0x79c / 2] = 0x20;
    {
        int16_t g = 0;
        (void)b25_gain_for_rate(&g, 0x20, -10);
        state[0x79e / 2] = g;
    }
    b25_init_level_tracker(state + 0x470 / 2);
    b25_init_level_tracker(state + 0x4f8 / 2);

    for (i = 0; i < 0x54; i++) {
        int16_t seed = state[0x648 / 2];
        b25_rand_step(&seed);
        state[0x648 / 2] = seed;
        state[0x64a / 2 + i] = seed;
    }

    lo = state;
    hi = state + 0x7a0 / 2;
    (void)b25_init_bit_alloc_and_agc(mode, hi, lo + 0x580 / 2);
}

void b25_quantize_coeffs_six_blocks(int16_t *payload, int32_t *pos, int16_t *vals, const uint16_t *allocs, int16_t total, int16_t encode, const int16_t *weight, const int16_t *loading)
{
    uint16_t q = (uint16_t)b25_div32((int32_t)total, 6);
    uint16_t r = (uint16_t)w32_sub(w32_mul(w32_add((int32_t)q, 1), 6), (int32_t)total);
    size_t v_off, a_off;

    b25_quantize_coeff_blocks(payload, pos, (int16_t)r, w16u_sub(q, 1), vals, allocs + 5, encode, weight, loading);

    v_off = (size_t)w16_mul((int16_t)q, (int16_t)r);
    a_off = 5 + (size_t)w16_mul((int16_t)w16u_sub(q, 1), (int16_t)r);
    b25_quantize_coeff_blocks(payload, pos, (int16_t)(6 - (int32_t)r), q, vals + v_off, allocs + a_off, encode, weight, loading);
}

void b25_expand_prba_to_log_mags(int16_t *out, uint16_t l, int16_t *prba, int16_t *mags, const int16_t *block_len, int16_t gain, const int16_t *cos_tbl)
{
    size_t i, dst;
    int16_t lg, offset;
    int32_t acc;

    b25_harmonic_idct(prba, 8, 8, cos_tbl);

    for (i = 0; i < 4; i++) {
        int32_t a = w32_mul((int32_t)prba[2 * i], 0x8000);
        int32_t b = w32_mul((int32_t)prba[2 * i + 1], 0x8000);
        int32_t d;
        prba[2 * i] = b25_high16_al2b1(w32_add(b, a));
        d = (int32_t)b25_high16_al2b1(w32_sub(a, b));
        prba[2 * i + 1] = b25_high16_al2b1(w32_mul(d, 0xb504));
    }

    dst = 0;
    for (i = 0; i < 4; i++) {
        int16_t n;
        mags[dst] = prba[2 * i];
        mags[dst + 1] = prba[2 * i + 1];
        n = block_len[i];
        b25_harmonic_idct(mags + dst, n, 6, cos_tbl);
        dst = dst + (size_t)n;
    }

    lg = (int16_t)b25_log2_at_q((int16_t)l, 0xf, 5);
    acc = w32_mul(w32_mul((int32_t)w16_sub(gain, lg), (int32_t)(int16_t)l), 2);
    for (i = 0; i < 4; i++) {
        int32_t t = w32_mul(w32_mul((int32_t)block_len[i], (int32_t)prba[2 * i]), 2);
        acc = w32_sub(acc, t);
    }
    offset = (int16_t)b25_div32_half(acc, (int16_t)l);

    if ((int16_t)l > 0) {
        int32_t base = w32_mul((int32_t)offset, 0x8000);
        for (i = 0; i < (size_t)l; i++) {
            int32_t x = w32_mul((int32_t)mags[i], 0x8000);
            out[i] = (int16_t)(b25_shl_sat(w32_add(x, base), 1) >> 16);
        }
    }
}

void b25_scale_to_target_energy(int16_t *buf, int16_t *exp, uint16_t n, int16_t target, int16_t target_exp)
{
    size_t nn = (int16_t)n > 0 ? (size_t)n : 0;
    int16_t src[56] = {0};
    int16_t e = 0;
    int32_t energy, g;
    int16_t den, m;
    int16_t scratch = 0;

    memcpy(src, buf, nn * sizeof(int16_t));
    b25_normalise_to_exp(buf, src, n, exp);

    energy = b25_dot_norm64(&e, buf, buf, n);
    e = w16_add(e, w16_mul(*exp, 2));

    den = b25_high16_al2b1(energy);
    if (den == 0) {
        return;
    }

    m = b25_div_norm(w32_mul((int32_t)target, 0x10000), target_exp, den, e, &scratch);
    g = b25_sqrt_norm(w32_mul((int32_t)m, 0x10000), &scratch);

    memcpy(src, buf, nn * sizeof(int16_t));
    b25_scale_vec(buf, src, (int16_t)n, b25_high16_al2b1(g));

    *exp = w16_add(*exp, scratch);
    if (*exp > 0) {
        memcpy(src, buf, nn * sizeof(int16_t));
        b25_scale_block_exp(buf, src, n, (int32_t)(uint16_t)*exp, 0);
        *exp = 0;
    }
}

int16_t b25_build_voiced_spectrum(int16_t *spec, int16_t *exp_out, int16_t w0, const int16_t *phase_resid, const int16_t *tags, const int16_t *mags, int32_t exp_in, int16_t kmax, int16_t kmin, int16_t tag, const int16_t *cos_tbl)
{
    uint16_t e = (uint16_t)b25_norm_exp_max_tagged(mags, tags, kmin, kmax, tag);
    int16_t cnt = 0;
    int32_t ex, r;
    b25_fill16(spec, 0, 0x100);

    if (kmin < kmax) {
        int16_t sh = w16_neg((int16_t)e);
        size_t idx = (size_t)(uint16_t)kmin;
        uint16_t kp1 = w16u_add((uint16_t)kmin, 1);
        uint16_t left = (uint16_t)w16_sub(kmax, kmin);
        for (;;) {
            if (tags[idx] == tag) {
                int32_t k = (int32_t)(int16_t)kp1;
                int32_t phase = w32_add(w32_mul(w32_add(w32_mul((int32_t)w0, k), 0x20), 0x400),
                                        w32_shl((int32_t)phase_resid[idx], 16) >> 6);
                int32_t f = (int32_t)b25_high16_al2b1(b25_frac_at_exp(phase, 6));
                uint32_t rr = b25_shift_round16_batch2(w32_mul(f, 0x400), 0xf);
                size_t ci = (size_t)(((int32_t)rr >> 16) & 0x1ff);
                int32_t m = (int32_t)mags[idx];
                int32_t p = w32_mul(w32_mul((int32_t)cos_tbl[ci], m), 2);
                size_t ci2;
                int32_t p2;
                spec[(size_t)k * 2] = b25_shifted_hi(p, sh);

                ci2 = (size_t)(w32_sub(w32_shl((int32_t)ci, 16), 0x7f0001) >> 16) & 0x1ff;
                p2 = w32_mul(w32_mul((int32_t)cos_tbl[ci2], m), 2);
                cnt = w16_add(cnt, 1);
                spec[(size_t)k * 2 + 1] = b25_shifted_hi(p2, sh);
            }
            kp1 = w16u_add(kp1, 1);
            idx += 1;
            left = w16u_sub(left, 1);
            if (left == 0) {
                break;
            }
        }
    }

    ex = w32_add(w32_add(exp_in, 0x17), (int32_t)e);
    *exp_out = (int16_t)ex;
    if (cnt < 1) {
        r = -0x20;
    } else {
        r = b25_inverse_real_fft(spec, ex, 8, 0);
    }
    *exp_out = (int16_t)r;
    spec[0x100] = spec[0];
    return cnt;
}
