/* C99 port of blip25-vocoder src/engine/decode.rs lines 2845-end (part decode_d). */
#include "b25_internal.h"

/* ======================= from aL3b2 ======================= */

/* 0x10310330 - decode a tone (signalling) frame */
int16_t b25_decode_tone_frame(int16_t *cfg, int16_t *prev, const int16_t *bits, int32_t *pos, const int16_t *alloc, const int16_t *tbl)
{
    int16_t amp_hi, amp2, n, tone, errs, lsb, r, level, st;
    int16_t rep[8] = {0};
    int32_t v;
    size_t k, lim;

    b25_read_field_i16(bits, pos, 6);
    amp_hi = (int16_t)b25_read_field_i16(bits, pos, 6);
    amp2 = w16_mul(amp_hi, 2);

    b25_fill16(rep, 0, 8);

    n = (int16_t)(((int32_t)alloc[2] - 0xd) >> 3);
    if (n > 0) {
        lim = (size_t)(uint16_t)n;
        if (lim > 8) lim = 8;
        for (k = 0; k < lim; k++) {
            rep[k] = (int16_t)b25_read_field_i16(bits, pos, 8);
        }
    }

    tone = rep[0];
    errs = 0;
    if (n > 1) {
        for (k = 1; k < (size_t)(uint16_t)n; k++) {
            int32_t d = (int32_t)rep[k] ^ (int32_t)rep[0];
            errs = w16_add(errs, (int16_t)b25_popcount_low((uint32_t)d, 8));
        }
    }
    if ((((uint8_t)rep[1] ^ (uint8_t)rep[0]) & 0xf0) != 0 || errs >= 6) {
        tone = -1;
    }

    lsb = (int16_t)b25_read_field_i16(bits, pos, 1);
    v = (int32_t)lsb | (int32_t)amp2;
    r = (int16_t)(w32_sub(w32_shl(v, 17), 0x00100001) >> 16);
    level = (int16_t)(w32_mul((int32_t)r, 0x0078f200) >> 16);

    st = (int16_t)(uint16_t)b25_pitch_config_init(cfg, tone, level, level, 0, tbl);
    if (st == 0) {
        b25_copy_params_l1_batch3(prev, cfg);
    }
    return st;
}

/* 0x103118c0 - seed each row's DC term and expand it with harmonic_idct */
size_t b25_expand_rows_idct(int16_t *dst, const int16_t *src, int16_t count, int16_t n, const int16_t *cos_tbl)
{
    ptrdiff_t stride, off;
    size_t i;
    if (count <= 0) {
        return 0;
    }
    stride = (ptrdiff_t)n;
    off = 0;
    for (i = 0; i < (size_t)(uint16_t)count; i++) {
        dst[(size_t)off] = src[i];
        b25_harmonic_idct(dst + (size_t)off, n, n, cos_tbl);
        off += stride;
    }
    return (size_t)off;
}

/* 0x10312fa0 - the encoder's IMBE frame packer */
void b25_pack_imbe_frame(int16_t *chan, int16_t nharm, const int16_t *hbits, const int16_t *alloc)
{
    b25_interleave_imbe_frame(chan, nharm, hbits, alloc, 1);
}

/* 0x10317e20 - halve the bottom eighth and shape the rest by headroom */
void b25_shape_by_headroom(int16_t *spec, int16_t *exp, int16_t n, int16_t scale, int16_t exp_scale, int16_t thresh, int16_t exp_thresh, const int16_t *reference)
{
    int16_t lo = (int16_t)(n >> 3);
    size_t keep = lo > 0 ? (size_t)lo : 0;
    int16_t head[7] = {0};
    int32_t e_blk;
    int16_t e, sh_scale, sh_thresh;
    int32_t t, g;
    uint16_t count;
    size_t k;

    memcpy(head, spec, keep * sizeof(int16_t));
    e_blk = (int32_t)(uint16_t)*exp;
    b25_rebase_block_exp(spec, head, (uint16_t)lo, e_blk, w32_add(e_blk, 1));

    e = w16_add(exp_thresh > exp_scale ? exp_thresh : exp_scale, 1);

    if (lo >= n) {
        *exp = w16_add(*exp, 1);
        return;
    }

    sh_scale = w16_sub(exp_scale, e);
    sh_thresh = w16_sub(exp_thresh, e);
    t = w32_shl((int32_t)thresh, 16);
    g = (int32_t)scale;

    count = w16u_sub((uint16_t)n, (uint16_t)lo);
    for (k = 0; k < (size_t)count; k++) {
        size_t i = (size_t)(uint16_t)lo + k;
        int32_t a = b25_shift32_al3b2(w32_mul(w32_mul((int32_t)reference[i], g), 2), sh_scale);
        int32_t margin = w32_sub(b25_shift32_al3b2(t, sh_thresh), a);
        int16_t gain;

        if (margin == 0) {
            gain = b25_clamp_gain(0, -1);
        } else {
            int16_t ex = e;
            int32_t s1 = b25_sqrt_norm(margin, &ex);
            int32_t arg, s2;
            ex = w16_add(ex, *exp);
            arg = w32_mul(w32_mul((int32_t)b25_high16_al2b3(s1), (int32_t)spec[i]), 2);
            s2 = b25_sqrt_norm(arg, &ex);
            if (ex < 2) {
                gain = b25_clamp_gain(s2, w16_sub(ex, 1));
            } else {
                gain = 0x4ccd;
            }
        }

        spec[i] = b25_high16_al2b3(w32_mul(w32_mul((int32_t)spec[i], (int32_t)gain), 2));
    }

    *exp = w16_add(*exp, 1);
}

/* gain clamp of shape_by_headroom */
int16_t b25_clamp_gain(int32_t v, int16_t sh)
{
    int16_t h = b25_high16_al2b3(b25_shift32_al3b2(v, sh));
    if (h > 0x4ccd) {
        return 0x4ccd;
    } else if (h < 0x2000) {
        return 0x2000;
    } else {
        return h;
    }
}

/* 0x10319330 - lay a whole bank of voiced harmonics into the synthesis buffer */
void b25_synth_harmonic_bank(int32_t *dst, int16_t *last_pos, int16_t *last_exp, int16_t step, const int16_t *table, int32_t exp, int32_t x0, int16_t k, int16_t a, int16_t c, uint16_t count, const int16_t *cos_tbl)
{
    int16_t pos[20] = {0};
    int16_t pexp[20] = {0};
    size_t i, last;
    b25_solve_quad_ramp(pos, pexp, x0, a, k, c, count);

    if ((int16_t)count > 0) {
        for (i = 0; i < (size_t)count; i++) {
            b25_synth_harmonic_span(dst, pos[i], pexp[i], step, table, exp, cos_tbl);
        }
    }

    last = (size_t)(int16_t)((int16_t)count - 1);
    *last_pos = pos[last];
    *last_exp = pexp[last];
}

/* ======================= from aL3b3 ======================= */

/* ETSI L_add, saturation limit from sgn */
int32_t b25_add_sat_sgn_al3b3(int32_t a, int32_t b, int32_t sgn)
{
    int32_t r = w32_add(a, b);
    if (((r ^ a) & (r ^ b)) < 0) {
        return (int32_t)(0x7fffffffu + (uint32_t)(sgn < 0));
    }
    return r;
}

/* ETSI L_sub, saturation limit from the minuend */
int32_t b25_sub_sat(int32_t a, int32_t b)
{
    int32_t r = w32_sub(a, b);
    if (((~b ^ r) & (r ^ a)) < 0) {
        return (int32_t)(0x7fffffffu + (uint32_t)(a < 0));
    }
    return r;
}

/* fill16 (0x1030b620) returning base + n words */
ptrdiff_t b25_fill_at(int16_t *buf, ptrdiff_t base, int16_t val, int16_t n)
{
    if (n > 0) {
        size_t a = (size_t)base;
        b25_fill16(buf + a, val, n);
    }
    return base + (ptrdiff_t)n;
}

/* 0x10310ff0 - fundamental from a coarse pitch-lag index */
void b25_fundamental_from_lag(int16_t mode, int16_t *w0, int16_t *l, int16_t lag, int16_t win, int16_t allow_high)
{
    uint32_t k;
    int16_t den, q, log_pitch;
    int32_t num;
    if (mode != 1) {
        *w0 = 0x4027;
        *l = 0xf;
        return;
    }
    k = ((uint32_t)w32_sub((int32_t)win, 6)) & 0x1f;
    den = (int16_t)w16u_mul(k >= 16 ? (uint16_t)0 : (uint16_t)(0x3cu << k), 2);
    num = w32_mul((int32_t)w16_add(w16_mul(lag, 2), 1), 0x2a52);
    q = (int16_t)b25_div32(num, den);
    log_pitch = w16_sub((int16_t)(uint16_t)0xbb03u, q);
    b25_fundamental_from_log2(w0, l, log_pitch, allow_high);
}

/* 0x10311ee0 - unvoiced (noise) spectral envelope */
void b25_unvoiced_envelope(int16_t *out, int16_t n, const int16_t *mags, int16_t l, int16_t level, int16_t mask)
{
    int16_t scratch[0x38] = {0};
    int32_t sum;
    int16_t coef;
    b25_attenuate_unvoiced_bands(scratch, mags, mask, level, l, 0);
    sum = b25_resample_lerp(out, (uint16_t)n, scratch, l);
    if (n < 0x10) {
        coef = 0x3333;
    } else if (n < 0x19) {
        coef = b25_high16_al2b3(w32_sub(w32_mul((int32_t)n, 0x3d70800), 0x6660000));
    } else {
        coef = 0x599a;
    }
    b25_scale_bias_vec(out, sum, n, coef);
}

/* 0x10314ae0 - average the neighbouring frames' envelopes */
void b25_average_neighbour_envelopes(int16_t *cur, const int16_t *prev, const int16_t *next)
{
    int16_t l = cur[2];
    int16_t a[0x38] = {0};
    int16_t b[0x38] = {0};
    int16_t i, n;
    b25_interp_envelope(a, cur[6], prev[6], prev + 8, (uint16_t)l);
    b25_interp_envelope(b, cur[6], next[6], next + 8, (uint16_t)l);

    i = 0;
    while (i < cur[2]) {
        int32_t s = w32_add((int32_t)b[(size_t)i], (int32_t)a[(size_t)i]) >> 1;
        int16_t v = (int16_t)s;
        if (v < -0x77ff) {
            v = -0x77ff;
        } else if (v > 0x77ff) {
            v = 0x77ff;
        }
        cur[8 + (size_t)i] = v;
        i = w16_add(i, 1);
    }

    n = cur[2];
    if (n < 0x38) {
        int16_t pad = cur[7 + (size_t)n];
        uint32_t k = (uint32_t)w16u_sub(0x38, (uint16_t)n);
        size_t d = 8 + (size_t)n;
        while (k != 0) {
            cur[d] = pad;
            d += 1;
            k -= 1;
        }
    }
}

/* 0x10318430 - shape the pre-filled noise spectrum band by band */
int16_t b25_shape_noise_bands(int16_t *spec, int16_t *exp_out, int16_t n, const int16_t *st, const int16_t *flags, int16_t zero_head, int16_t scale)
{
    int mode2 = st[0] == 2;
    uint16_t half = (uint16_t)(n >> 1);
    int16_t limit = n == 0x200 ? 0xf2 : 0x79;
    uint16_t peak = (uint16_t)b25_peak_exp_unflagged(st + 8, flags, st[2]);
    uint16_t exp = 0x11;
    int16_t mant;
    uint16_t total;
    int32_t nn, step, acc;
    uint16_t idx, rem;
    ptrdiff_t ptr;
    int16_t voiced_bands;

    if (mode2) {
        int16_t e = (int16_t)exp;
        int32_t r = b25_sqrt_norm(w32_mul((int32_t)st[2], 0x86b6), &e);
        int32_t c = w32_mul((int32_t)(int16_t)(r >> 16), 0xaff4);
        exp = w16u_add((uint16_t)e, w16u_add((uint16_t)*exp_out, 1));
        mant = b25_high16_al2b3(c);
    } else {
        int16_t e = (int16_t)exp;
        int32_t x = (int32_t)w16_mul(st[2], scale);
        int32_t r = b25_sqrt_norm(w32_mul(x, 0x86b6), &e);
        exp = (uint16_t)e;
        mant = (int16_t)(r >> 16);
    }

    total = w16u_add(w16u_add(exp, peak), 2);
    nn = (int32_t)n;
    step = w32_mul(w32_mul((int32_t)st[6], nn), 2) >> 4;

    acc = w32_add(step, 0x10000) >> 1;
    idx = (uint16_t)(int16_t)(acc >> 16);
    ptr = b25_fill_at(spec, 0, 0, (int16_t)w16u_mul(idx, 2));

    voiced_bands = 0;
    if (0 < st[2]) {
        int16_t sh = w16_neg((int16_t)peak);
        int16_t src[112] = {0};
        int16_t i = 0;
        for (;;) {
            uint16_t prev;
            int16_t cur, cnt;
            acc = w32_add(acc, step);
            prev = idx;
            cur = (int16_t)(acc >> 16);
            if (cur > (int16_t)half) {
                cur = (int16_t)half;
            }
            idx = (uint16_t)cur;
            cnt = (int16_t)w16u_sub(idx, prev);

            if (flags[(size_t)i] != 0) {
                b25_fill_at(spec, ptr, 0, w16_mul(cnt, 2));
            } else {
                int16_t m, e, e2, coef;
                int32_t p;
                size_t a, w;
                if (mode2) {
                    m = mant;
                    e = (int16_t)exp;
                } else {
                    int16_t rm = 0, re = 0;
                    b25_vector_rms(&rm, &re, spec + (size_t)ptr, cnt);
                    m = b25_high16_al2b3(w32_mul(w32_mul((int32_t)rm, (int32_t)mant), 2));
                    e = w16_add(re, (int16_t)exp);
                }
                p = b25_shift32_al3b2(w32_mul(w32_mul((int32_t)st[8 + (size_t)i], (int32_t)m), 2), sh);
                e2 = w16_add(e, (int16_t)peak);
                coef = b25_high16_al2b3(p);
                a = (size_t)ptr;
                w = (size_t)cnt * 2;
                if (cnt > 0) {
                    memcpy(src, spec + a, w * sizeof(int16_t));
                }
                b25_scale_vec_shift(spec + a, src, w16_mul(cnt, 2), coef, e2, (int16_t)total);
                voiced_bands = w16_add(voiced_bands, 1);
            }
            ptr += (ptrdiff_t)cnt * 2;
            i = w16_add(i, 1);
            if (i >= st[2]) {
                break;
            }
        }
    }

    rem = (uint16_t)w16_sub((int16_t)half, (int16_t)idx);
    if (((int32_t)(int16_t)half - (int32_t)limit) > (int32_t)(int16_t)rem) {
        rem = (uint16_t)w16_sub((int16_t)half, limit);
    }
    b25_fill_at(spec, (ptrdiff_t)(nn - 2 * (int32_t)(int16_t)rem), 0, (int16_t)w16u_mul(rem, 2));
    b25_fill_at(spec, 0, 0, (int16_t)w32_mul(w32_mul(w32_mul((int32_t)zero_head, nn), 2) >> 0x14, 2));

    *exp_out = w16_add(st[0x42], (int16_t)total);
    return voiced_bands;
}

/* 0x1031fe40 - quantise (or dequantise) the frame gain */
void b25_quantise_gain(int16_t *gain, int16_t mode, const int16_t *mags, int16_t nmags, int16_t *prev, int16_t nbits, const int16_t *book, int16_t *bits, int32_t *pos, int16_t encode)
{
    int16_t g;
    if (encode != 0) {
        int16_t lg;
        int32_t s1, s2, s3;
        gain[0] = (int16_t)b25_mean_i16(mags, (uint16_t)nmags);
        lg = (int16_t)b25_log2_at_q(nmags, 0xf, 5);
        s1 = b25_add_sat_sgn_al3b3(w32_shl((int32_t)lg, 16), w32_shl((int32_t)gain[0], 16), w32_shl((int32_t)gain[0], 16));
        s2 = b25_sub_sat(s1, w32_shl((int32_t)*prev, 15));
        s3 = b25_shl_sat(s2, 0);
        gain[0] = b25_high16_al2b3(b25_add_sat_sgn_al3b3(s3, 0x8000, s3));
        if (mode == 2) {
            int32_t v = w32_shl((int32_t)gain[0], 16);
            gain[0] = b25_high16_al2b3(b25_add_sat_sgn_al3b3(v, 0x10000000, v));
        }
    }

    b25_codebook_field(gain, 1, nbits, book, nbits, 1, bits, pos, encode);

    if (mode == 2) {
        gain[0] = b25_high16_al2b3(w32_sub(w32_shl((int32_t)gain[0], 16), 0x0fff0001));
    }
    g = b25_high16_al2b3((int32_t)w32u_shl((uint32_t)w32_add((int32_t)*prev, w32_mul((int32_t)gain[0], 2)), 15));
    if (g > 0x67ff) {
        g = 0x6800;
    }
    gain[0] = g;
    if (mode != 2) {
        *prev = g;
    }
}

/* ======================= from aL4b1 ======================= */

/* normalisation shift (bsr-based) */
uint16_t b25_norm_shift_al4b1(int32_t x)
{
    uint32_t mag, msb;
    if (x == 0) {
        return 0;
    }
    mag = (uint32_t)(x < 0 ? ~x : x);
    msb = mag == 0 ? 0 : 31 - lz32(mag);
    return (uint16_t)(0x1eu - msb);
}

/* shift left and take the high word */
int16_t b25_shl_hi16(int32_t x, uint16_t sh)
{
    return (int16_t)((int32_t)((uint32_t)x << ((uint32_t)sh & 0x1f)) >> 16);
}

uint32_t b25_get32_al4b1(const int16_t *w, size_t i)
{
    return (uint32_t)(uint16_t)w[i] | ((uint32_t)(uint16_t)w[i + 1] << 16);
}

void b25_put32_al4b1(int16_t *w, size_t i, uint32_t v)
{
    w[i] = (int16_t)(uint16_t)v;
    w[i + 1] = (int16_t)(uint16_t)(v >> 16);
}

/* 0x10314980 - choose the parameters a frame is synthesised from */
void b25_select_repeat_params(int16_t *dst, const int16_t *cur, const int16_t *prev)
{
    const int16_t *src;
    if ((uint16_t)cur[1] != 0xff || (uint16_t)prev[1] != 0xff) {
        b25_copy_params_l1_batch3(dst, cur);
        return;
    }

    if (prev[0] == 2) {
        uint32_t mask = b25_get32_al4b1(dst, 4);
        int16_t lvl = dst[7];
        b25_copy_params_l1_batch3(dst, cur);
        b25_put32_al4b1(dst, 4, mask);
        dst[7] = lvl;
        if (cur[0] != 2) {
            size_t i;
            for (i = 8; i < 8 + 0x38; i++) {
                dst[i] = (int16_t)(dst[i] >> 1);
            }
        }
        return;
    }

    if (cur[0] != 2) {
        uint16_t w0 = b25_merge_voiced_amplitude(b25_get32_al4b1(dst, 4), cur, prev);
        dst[6] = (int16_t)w0;
        dst[2] = b25_harmonic_count((int16_t)w0);
        dst[1] = cur[1];
        b25_average_neighbour_envelopes(dst, cur, prev);
        return;
    }

    if (prev[0x43] < 2 && prev[0] != 3) {
        src = prev;
    } else {
        src = cur;
    }
    b25_copy_params_l1_batch3(dst, src);
    if (dst[7] > 0) {
        dst[7] = w16_sub(dst[7], 0x5000);
    } else {
        b25_put32_al4b1(dst, 4, b25_get32_al4b1(dst, 4) & 0x55555555u);
        dst[7] = 0;
    }
}

/* 0x10317aa0 - one pass of voicing-driven spectral shaping */
void b25_shape_spectrum_by_voicing(int16_t *amps, int16_t *exp, int16_t *level, int16_t *level_exp, int16_t omega, int16_t n, const int16_t *cos_tbl)
{
    int16_t cosines[56] = {0};
    int16_t e_m = 0, e_e = 0, c_m = 0, c_e = 0;
    int16_t energy, energy_exp;
    b25_harmonic_energy_and_corr(&e_m, &e_e, &c_m, &c_e, cosines, omega, amps, *exp, (uint16_t)n, cos_tbl);

    energy = e_m;
    energy_exp = e_e;
    b25_track_level(level, level_exp, e_m, e_e);

    if (energy > 0) {
        int16_t a0 = e_m, b0 = e_e, c0 = c_m, d0 = c_e;
        int16_t gain = 0;
        b25_voicing_gains(&gain, &e_m, &c_m, &c_e, a0, b0, c0, d0, omega);
        b25_shape_by_headroom(amps, exp, n, gain, e_m, c_m, c_e, cosines);
        b25_scale_to_target_energy(amps, exp, (uint16_t)n, energy, energy_exp);
    }
}

/* 0x103181f0 - the unvoiced half of one synthesis subframe */
void b25_synthesise_noise_subframe(int32_t *acc, int16_t n, int16_t *st, const int16_t *params, const int16_t *flags, int16_t zero_head, const int16_t *sqrt_tbl)
{
    int32_t log2n = 8;
    int16_t fftlen;
    int16_t win[86] = {0};
    int16_t spec[256] = {0};
    int16_t exp_out;

    if (n > 0x80) {
        int32_t k = 8;
        for (;;) {
            int32_t probe;
            log2n = k + 1;
            probe = w32_shl(1, (uint32_t)(k & 0x1f));
            k = log2n;
            if (probe >= (int32_t)n) {
                break;
            }
        }
    }
    if ((log2n & 0x1f) >= 16) {
        fftlen = 0;
    } else {
        fftlen = (int16_t)(uint16_t)(1u << (uint32_t)(log2n & 0x1f));
    }

    b25_resample_linear(win, n, sqrt_tbl, 0x50);

    if (params[0] == 3) {
        b25_fill16(st + 1, 0, 0x54);
        b25_fill16(spec, 0, 0x100);
        exp_out = 0;
    } else {
        int16_t e;
        b25_window_overlap(spec, &st[0], st + 1, win, (uint16_t)n, fftlen);
        e = (int16_t)(uint16_t)b25_rfft_forward(spec, 0, (int16_t)log2n, 0);
        b25_shape_noise_bands(spec, &e, fftlen, params, flags, zero_head, n);
        exp_out = (int16_t)(uint16_t)b25_inverse_real_fft(spec, (int32_t)(uint16_t)e, log2n, 0);
    }

    b25_correlate_and_shift(acc, spec, exp_out, st + 0x55, st + 0xa9, win, n);
}

/* word offsets of the four per-block coefficient codebooks */
const size_t DEC_COEF_BOOK_OFF[4] = {0, 128, 192, 256};

/* index widths of the four per-block coefficient codebooks */
const int16_t DEC_COEF_BOOK_BITS[4] = {5, 4, 4, 3};

/* 0x10311250 - code (or decode) a frame's spectral amplitudes */
void b25_code_amplitudes(const int16_t *prev, int16_t *cur, int16_t gain, const int16_t *cfg, int16_t *bits, int32_t *pos, int16_t encode, const int16_t *block_lens, const int16_t *prba_book9, const int16_t *prba_book7, const int16_t *coef_books, const int16_t *cos_tbl)
{
    uint16_t l = (uint16_t)cur[2];
    int16_t pred[56] = {0};
    int32_t sum;
    uint16_t packed;
    int16_t lens[4] = {0};
    int16_t prba[8] = {0};
    size_t base, k, i;
    int16_t out[0x38];
    int16_t mags[0x38];

    sum = b25_resample_lerp(pred, l, prev + 8, prev[2]);
    b25_scale_bias_vec(pred, sum, (int16_t)l, 0x5333);

    packed = (uint16_t)block_lens[(size_t)w16_sub((int16_t)l, 9)];
    for (k = 0; k < 4; k++) {
        lens[k] = w16_add((int16_t)(packed & 0xf), 2);
        packed = (uint16_t)((int16_t)packed >> 4);
    }

    if (encode != 0) {
        int16_t a[0x38];
        uint16_t lens_u[4];
        memcpy(a, cur + 8, 0x38 * sizeof(int16_t));
        lens_u[0] = (uint16_t)lens[0];
        lens_u[1] = (uint16_t)lens[1];
        lens_u[2] = (uint16_t)lens[2];
        lens_u[3] = (uint16_t)lens[3];
        b25_prba_transform(prba, cur + 8, pred, a, l, lens_u, cos_tbl);
    }
    prba[0] = 0;

    b25_codebook_field(prba + 1, 3, cfg[7], prba_book9, 9, 3, bits, pos, encode);
    b25_codebook_field(prba + 4, 4, cfg[8], prba_book7, 7, 4, bits, pos, encode);

    base = 0;
    for (k = 0; k < 4; k++) {
        int16_t coeffs = w16_sub(lens[k], 2);
        int16_t avail = cfg[9 + k];
        if (coeffs < 1) {
            if (encode == 0) {
                b25_read_field_i16(bits, pos, avail);
            } else {
                size_t cur_pos = (size_t)*pos;
                b25_emit_bits(&cur_pos, bits, avail, 0);
                *pos = (int32_t)cur_pos;
            }
        } else {
            uint16_t nwords = (uint16_t)(coeffs > 4 ? 4 : coeffs);
            b25_codebook_field(cur + 8 + base + 2, nwords, avail, coef_books + DEC_COEF_BOOK_OFF[k], DEC_COEF_BOOK_BITS[k], 4, bits, pos, encode);
        }
        base = base + (size_t)lens[k];
    }

    memcpy(out, cur + 8, 0x38 * sizeof(int16_t));
    memcpy(mags, out, 0x38 * sizeof(int16_t));
    b25_expand_prba_to_log_mags(out, l, prba, mags, lens, gain, cos_tbl);
    for (i = 0; i < 0x38; i++) {
        cur[8 + i] = i < (size_t)l ? out[i] : mags[i];
    }

    b25_avg_and_extend(cur + 8, pred, l);
}

/* 0x10311ba0 - code (or decode) a frame's log-magnitude residual and gain */
void b25_code_log_mag_residual(int16_t *bits, int32_t *pos, int16_t *prev, int16_t *cur, const uint16_t *alloc, int16_t encode, const int16_t *gain_book, const int16_t *weight, const int16_t *loading, const int16_t *band_tbl, const int16_t *cos_tbl)
{
    int16_t l = cur[2];
    uint16_t q = (uint16_t)b25_div32((int32_t)l, 6);
    uint16_t split = (uint16_t)((((uint32_t)q * 3u + 3u) * 2u) - (uint32_t)(uint16_t)l);
    int16_t pred[56] = {0};
    int16_t means[6] = {0};
    int16_t gain = 0;
    uint16_t nbits[5];
    size_t off2;

    b25_unvoiced_envelope(pred, l, prev + 8, prev[2], prev[6], prev[0x45]);

    if (encode != 0) {
        int32_t off;
        b25_vsub_sat(cur + 8, pred, (uint16_t)l);
        if (cur[0] == 3) {
            gain = (int16_t)b25_band_pair_mean(cur + 8, cur[1], band_tbl);
        }
        off = b25_block_dft_dc(means, cur + 8, split, (uint32_t)q, cos_tbl);
        b25_block_dft_dc(means + (size_t)split, cur + 8 + (size_t)off, w16u_sub(6, split), (uint32_t)q + 1u, cos_tbl);
        b25_harmonic_dft(means, 6, 6, cos_tbl);
        if (cur[0] != 3) {
            gain = means[0];
        }
    }
    means[0] = 0;

    nbits[0] = alloc[0];
    nbits[1] = alloc[1];
    nbits[2] = alloc[2];
    nbits[3] = alloc[3];
    nbits[4] = alloc[4];
    b25_code_higher_order_coeffs(bits, pos, nbits, means, encode != 0);
    b25_quantize_coeffs_six_blocks(bits, pos, cur + 8, alloc, l, encode, weight, loading);

    b25_harmonic_idct(means, 6, 6, cos_tbl);
    off2 = b25_expand_rows_idct(cur + 8, means, (int16_t)split, (int16_t)q, cos_tbl);
    b25_expand_rows_idct(cur + 8 + off2, means + (size_t)split, (int16_t)(6 - (int32_t)split), w16_add((int16_t)q, 1), cos_tbl);

    if (encode == 0) {
        int16_t idx = (int16_t)b25_read_field_i16(bits, pos, 6);
        gain = gain_book[(size_t)idx];
    } else {
        int16_t idx;
        size_t cursor;
        if (cur[0] == 3) {
            gain = w16_sub(gain, (int16_t)b25_band_pair_mean(cur + 8, cur[1], band_tbl));
        }
        idx = b25_quantize_codebook64(&gain);
        cursor = (size_t)*pos;
        b25_emit_bits(&cursor, bits, 6, idx);
        *pos = (int32_t)cursor;
    }

    b25_vec_add3_clamp(cur + 8, pred, (uint16_t)l, gain);

    if (encode == 0 && l < 0x38) {
        size_t i;
        for (i = 0; i < (size_t)w16u_sub(0x38, (uint16_t)l); i++) {
            cur[8 + (size_t)l + i] = cur[7 + (size_t)l + i];
        }
    }
    prev[0x42] = gain;
}

int32_t b25_rd32_al4b1(const int16_t *w, size_t i)
{
    return (int32_t)b25_get32_al4b1(w, i);
}

void b25_wr32_al4b1(int16_t *w, size_t i, int32_t v)
{
    b25_put32_al4b1(w, i, (uint32_t)v);
}

/* 0x10318b00 - synthesise one voiced half-frame and cross-fade it in */
void b25_synthesise_voiced_subframe(int32_t *acc, int16_t n, int16_t *state, const int16_t *cur, const int16_t *cur_tags, const int16_t *prev, int16_t p6, const int16_t *cos_tbl)
{
    int16_t last_pos = state[0xaa];
    int16_t last_exp = state[0xab];
    int16_t prev_voiced = (int16_t)((b25_get32_al4b1(prev, 4) & 0x55555555u) != 0);
    int16_t cur_voiced = (int16_t)((b25_get32_al4b1(cur, 4) & 0x55555555u) != 0);
    int32_t nsig = (int32_t)n;
    int32_t nq = w32_shl(nsig, 16);
    uint16_t sh = b25_norm_shift_al4b1(nq);
    int16_t fade_exp = (int16_t)w16u_sub(0x0f, sh);
    int16_t fade_step;
    int32_t pitch, pitch_a, refined, saved, acc_b, w_prev, out_acc;
    int16_t ra = 0, rb = 0, f_half = 0, f_dbl = 0;
    int16_t count, ca, cb, kmin, voiced_cnt;
    int32_t buf[168];
    size_t i;
    uint16_t sh3;
    int16_t m;

    fade_step = (int16_t)(uint16_t)b25_div_norm(0x40000000, 1, b25_shl_hi16(nq, sh), fade_exp, &fade_exp);

    pitch = b25_rd32_al4b1(state, 0xa8);
    pitch_a = pitch;
    refined = b25_refine_pitch(pitch, &ra, &rb, &f_half, &f_dbl, prev[6], cur[6], prev_voiced, cur_voiced, n);
    count = (int16_t)(refined >> 16);
    saved = refined;
    acc_b = refined;
    ca = ra;
    cb = rb;

    memset(buf, 0, sizeof(buf));
    for (i = 0; i < 84; i++) {
        buf[i] = b25_rd32_al4b1(state, 2 * i);
    }

    if (prev_voiced != 0 && ((int16_t)(refined >> 16)) > 0) {
        int32_t exp = (int32_t)(uint16_t)state[0xe5];
        b25_synth_harmonic_bank(buf, &last_pos, &last_exp, prev[6], state + 0xe6, exp, pitch, rb, ra, n, (uint16_t)count, cos_tbl);
    }
    b25_ramp_mix(acc, buf, fade_step, fade_exp, n);
    memset(buf, 0, sizeof(buf));

    w_prev = w32_neg(w32_shl((int32_t)cur[6], 5));
    kmin = (int16_t)(uint16_t)b25_ratio_q15(p6, cur[6]);
    voiced_cnt = b25_build_voiced_spectrum(state + 0xe6, state + 0xe5, (int16_t)(w32_shl(w_prev, 0xb) >> 0x10), state + 0xad, cur_tags, cur + 8, (int32_t)(uint16_t)cur[0x42], cur[2], kmin, 2, cos_tbl);

    if (voiced_cnt != 0) {
        int32_t v = w32_add(w32_shl((int32_t)cur[7], 16) >> 8, w32_shl(w32_sub(nsig, 0x10), 16));
        uint16_t sh2 = b25_norm_shift_al4b1(v);
        int32_t exp = (int32_t)(uint16_t)state[0xe5];
        b25_synth_harmonic_span(buf, b25_shl_hi16(v, sh2), (int16_t)w16u_sub(0x0f, sh2), cur[6], state + 0xe6, exp, cos_tbl);
    }

    out_acc = saved;
    if (cur_voiced != 0) {
        int32_t w_cur, exp;
        if (f_half == 0) {
            if (f_dbl != 0) {
                int32_t half;
                cb = (int16_t)(w32_shl((int32_t)rb, 16) >> 0x11);
                half = b25_rd32_al4b1(state, 0xa8) >> 1;
                ca = (int16_t)(w32_shl((int32_t)ra, 16) >> 0x11);
                out_acc = w32_add(w32_sub(saved, b25_rd32_al4b1(state, 0xa8)) >> 1, half);
                b25_wr32_al4b1(state, 0xa8, half);
                pitch = half;
            }
        } else {
            b25_pitch_advance(&pitch_a, &acc_b, &ra, &rb, prev[6], state);
            pitch = pitch_a;
            ca = ra;
            cb = rb;
            out_acc = acc_b;
        }
        count = (int16_t)(out_acc >> 16);

        w_cur = w32_neg(w32_shl((int32_t)cur[6], 5));
        b25_build_voiced_spectrum(state + 0xe6, state + 0xe5, (int16_t)(w32_shl(w_cur, 0xb) >> 0x10), state + 0xad, cur_tags, cur + 8, (int32_t)(uint16_t)cur[0x42], cur[2], 0, 1, cos_tbl);
        exp = (int32_t)(uint16_t)state[0xe5];
        b25_synth_harmonic_span(buf, state[0xaa], state[0xab], cur[6], state + 0xe6, exp, cos_tbl);
        if (count > 0) {
            b25_synth_harmonic_bank(buf, &last_pos, &last_exp, cur[6], state + 0xe6, exp, pitch, cb, ca, n, (uint16_t)count, cos_tbl);
        }
    }

    b25_wr32_al4b1(state, 0xa8, w32_sub(out_acc, w32_shl((int32_t)count, 16)));
    b25_mac_ramp(acc, buf, fade_step, fade_exp, n);
    for (i = 0; i < 84; i++) {
        b25_wr32_al4b1(state, 2 * i, buf[(size_t)n + i]);
    }

    if (prev_voiced == 0 && cur_voiced == 0) {
        b25_put32_al4b1(state, 0xaa, 0);
        return;
    }
    sh3 = b25_norm_shift_al4b1(nq);
    m = b25_sub_norm(b25_shl_hi16(nq, sh3), (int16_t)w16u_sub(0x0f, sh3), last_pos, last_exp, &state[0xab]);
    state[0xaa] = m;
}

/* ======================= from aL5b1 ======================= */

int32_t b25_get32_al5b1(const int16_t *w, size_t i)
{
    return (int32_t)((uint32_t)(uint16_t)w[i] | ((uint32_t)(uint16_t)w[i + 1] << 16));
}

void b25_put32_al5b1(int16_t *w, size_t i, int32_t v)
{
    w[i] = (int16_t)(uint16_t)v;
    w[i + 1] = (int16_t)(uint16_t)(v >> 16);
}

/* 0x1030c520 - synthesise one 10 ms half-frame of PCM */
void b25_dec_synth_subframe(int16_t *params, int16_t *out, int16_t n, int16_t half, int16_t *state, const int16_t *hilbert, const int16_t *cos_tbl, const int16_t *sqrt_tbl)
{
    int16_t tags[DEC_TAG_WORDS] = {0};
    int32_t acc[84] = {0};
    int16_t level;
    int32_t reson[6];
    int32_t src[84];
    size_t k;

    if (((uint16_t)state[DEC_S_BYPASS] & 1) == 0) {
        if (params[0] == 1) {
            b25_spectral_slope(state + DEC_S_SLOPE, params + 8, params[2], hilbert);
        } else {
            b25_fill16(state + DEC_S_SLOPE, 0, 0x38);
        }

        {
            uint16_t count = (uint16_t)params[2];
            b25_expand_log_mags(params + 8, params + 0x42, count);
        }

        if (params[0] == 1) {
            int16_t omega = params[6];
            int16_t count = params[2];
            b25_shape_spectrum_by_voicing(params + 8, params + 0x42, state + DEC_S_SHAPE_LEVEL, state + DEC_S_SHAPE_EXP, omega, count, cos_tbl);
        }

        if (params[0] == 2) {
            b25_copy_params_reset_phase(state + DEC_S_TONE, params);
        }
    }

    b25_voicing_setup(params, tags);

    if (params[1] == 0xff) {
        if (half == 0) {
            uint32_t flags = (uint32_t)b25_get32_al5b1(params, 4);
            state[DEC_S_LEVEL] = (int16_t)b25_smooth_level_if_voiced((uint16_t)state[DEC_S_LEVEL], params[6], flags);
        }
        b25_weight_harmonics_by_freq_sq(params, state[DEC_S_LEVEL]);
        b25_refine_mags(params);
    } else if (state[DEC_S_PITCH_MSG] == 1) {
        b25_retune_from_pitch_msg(params, tags, state + DEC_S_PREV_AL5B1);
    } else {
        b25_tone_frame_fixup(params, cos_tbl);
    }

    level = state[DEC_S_LEVEL];

    b25_synthesise_noise_subframe(acc, n, state + DEC_S_NOISE, params, tags, level, sqrt_tbl);

    b25_synthesise_voiced_subframe(acc, n, state + DEC_S_VOICED, params, tags, state + DEC_S_PREV_AL5B1, level, cos_tbl);

    for (k = 0; k < 6; k++) {
        reson[k] = b25_get32_al5b1(state, DEC_S_RESON + 2 * k);
    }
    memcpy(src, acc, sizeof(src));
    b25_resonator3(acc, src, reson, n);
    for (k = 0; k < 6; k++) {
        b25_put32_al5b1(state, DEC_S_RESON + 2 * k, reson[k]);
    }

    b25_round_block_i32(out, 0xf, acc, 0x11, (uint16_t)n);

    params[0x40] = 0;
    params[0x41] = 0;
    b25_copy_params_reset_phase(state + DEC_S_PREV_AL5B1, params);
}

/* 0x10311470 - code (or decode) a frame's gain and then its amplitudes */
void b25_code_gain_and_amplitudes(int16_t *prev, int16_t *cur, const int16_t *cfg, int16_t *bits, int32_t *pos, int16_t encode, const int16_t *gain_book, const int16_t *block_lens, const int16_t *prba_book9, const int16_t *prba_book7, const int16_t *coef_books, const int16_t *cos_tbl)
{
    int16_t gain[1] = {0};
    {
        int16_t mode = cur[0];
        int16_t nmags = cur[2];
        b25_quantise_gain(gain, mode, cur + 8, nmags, &prev[0x42], cfg[6], gain_book, bits, pos, encode);
    }
    b25_code_amplitudes(prev, cur, gain[0], cfg, bits, pos, encode, block_lens, prba_book9, prba_book7, coef_books, cos_tbl);
}

/* 0x10311fa0 - code (or decode) the log-magnitude residual with voicing */
void b25_code_log_mags_with_voicing(int16_t *prev, int16_t *cur, int16_t mask, const uint16_t *alloc, int16_t *bits, int32_t *pos, int16_t encode, const int16_t *gain_book, const int16_t *weight, const int16_t *loading, const int16_t *band_tbl, const int16_t *cos_tbl)
{
    if (encode != 0) {
        b25_attenuate_in_place(cur, mask, 0);
    }

    b25_code_log_mag_residual(bits, pos, prev, cur, alloc, encode, gain_book, weight, loading, band_tbl, cos_tbl);

    b25_attenuate_in_place(cur, mask, 1);

    if (encode != 0) {
        size_t cursor = (size_t)*pos;
        b25_emit_bits(&cursor, bits, 1, (int16_t)(prev[0x44] & 1));
        *pos = (int32_t)cursor;
        prev[0x44] = w16_add(prev[0x44], 1);
    }
}

/* attenuate_unvoiced_bands in place on cur[8..] */
void b25_attenuate_in_place(int16_t *cur, int16_t mask, int16_t negate)
{
    int16_t level = cur[6];
    int16_t nbits = cur[2];
    int16_t src[DEC_TAG_WORDS];
    memcpy(src, cur + 8, DEC_TAG_WORDS * sizeof(int16_t));
    b25_attenuate_unvoiced_bands(cur + 8, src, mask, level, nbits, negate);
}

/* ======================= from aL6b1 ======================= */

uint32_t b25_get32_al6b1(const int16_t *w, size_t i)
{
    return (uint32_t)(uint16_t)w[i] | ((uint32_t)(uint16_t)w[i + 1] << 16);
}

uint16_t b25_shl16_al6b1(uint16_t v, int32_t count)
{
    return (uint16_t)((uint32_t)v << ((uint32_t)count & 0x1f));
}

/* 0x1030ba70 - decode one AMBE half-rate frame's payload */
int16_t b25_decode_frame_ambe(int16_t *cur, int16_t *synth, int16_t *prev, int16_t *bits, int16_t rate, const int16_t *ctrl, int16_t *alloc, int16_t allow_high, const int16_t *submode_tbl, const uint32_t *voicing_tbl, const int16_t *gain_book, const int16_t *block_lens, const int16_t *prba_book9, const int16_t *prba_book7, const int16_t *coef_books, const int16_t *blob)
{
    int32_t pos;
    int16_t code, cls;

    if (alloc[0] == 0) {
        b25_imbe_bit_alloc(alloc);
    } else {
        int32_t sub = w32_sub(0x31, (int32_t)(uint16_t)submode_tbl[(size_t)(int32_t)rate]);
        b25_ambe_bit_alloc(alloc, (int16_t)sub);
    }

    pos = 0;
    code = (int16_t)b25_pack_bits_i16(bits, 6);

    if ((uint16_t)code == 0x3f) {
        cls = 3;
    } else if ((uint16_t)code == 0x3e) {
        cls = 2;
    } else {
        cls = (int16_t)(code < 0x3c);
    }

    if (cls <= 0) {
        prev[DEC_S_TRACK] = -3;
        return 4;
    }

    if (cls <= 2) {
        int16_t lag, vidx;
        int32_t split;
        synth[DEC_P_CLASS] = cls;
        cur[DEC_P_CLASS] = cls;
        synth[DEC_P_TAG] = 0xff;
        cur[DEC_P_TAG] = 0xff;

        b25_frame_bit_permute(bits, alloc, 1);

        lag = (int16_t)b25_read_field_i16(bits, &pos, alloc[5]);
        vidx = (int16_t)b25_read_field_i16(bits, &pos, alloc[4]);

        {
            uint32_t m_synth = b25_get32_al6b1(synth, DEC_P_VOICE);
            uint32_t m_cur = b25_get32_al6b1(cur, DEC_P_VOICE);
            b25_voicing_from_table(cls, &m_synth, &m_cur, vidx, (int32_t)(uint16_t)alloc[4], voicing_tbl);
            b25_put32_al4b1(synth, DEC_P_VOICE, m_synth);
            b25_put32_al4b1(cur, DEC_P_VOICE, m_cur);
        }

        split = b25_split_voiced_freq_code(&lag, synth, cur, 0, 0, (uint32_t)(uint16_t)alloc[5], 0);

        if (split != 0) {
            synth[DEC_P_W0] = 0x1079;
            cur[DEC_P_W0] = 0x1079;
            synth[DEC_P_L] = 0x38;
            cur[DEC_P_L] = 0x38;
        } else {
            int16_t mode = cur[DEC_P_CLASS];
            b25_fundamental_from_lag(mode, &cur[DEC_P_W0], &cur[DEC_P_L], lag, alloc[5], allow_high);
        }

        b25_code_gain_and_amplitudes(prev, cur, alloc, bits, &pos, 0, gain_book, block_lens, prba_book9, prba_book7, coef_books, blob);
        b25_suppress_spectral_peaks(cur, prev, ctrl[DEC_C_FRAMES], ctrl[DEC_C_SUBMODE]);
    } else {
        int16_t status = b25_decode_tone_frame(cur, synth, bits, &pos, alloc, blob);
        if (status != 0) {
            prev[DEC_S_TRACK] = -3;
            return status;
        }
    }

    b25_select_repeat_params(synth, cur, prev);
    b25_copy_params_batch2(prev, cur, 0);
    prev[DEC_S_TRACK] = -3;
    return 0;
}

/* 0x1030bfd0 - decode one IMBE frame's payload */
int16_t b25_decode_frame_imbe(int16_t *cur, int16_t *synth, int16_t *prev, int16_t *bits, int16_t voffs, const int16_t *ctrl, int16_t *alloc, int16_t mant, int16_t exp, const int16_t *nib_tbl, const int16_t *gain_book, const int16_t *weight, const int16_t *loading, const uint16_t *codewords, const int16_t *blob)
{
    int32_t pos;
    uint16_t coarse;
    int32_t widen;
    uint16_t hbits[56] = {0};
    int16_t hbits_i[56] = {0};
    uint32_t pitch;
    int16_t triads, nb;
    uint16_t vfield, mask;
    int32_t vnarrow, idx;
    uint32_t dibits, pattern, synth_mask, cur_mask;
    int16_t synth_offs;
    size_t i;

    if (alloc[0] == 0) {
        b25_imbe_bit_alloc(alloc);
    } else {
        b25_ambe_bit_alloc(alloc, 0);
    }

    pos = 0;
    coarse = b25_pack_bits_i16(bits, 6);

    {
        int16_t src[2];
        src[0] = prev[DEC_S_TONE_HIST + 1];
        src[1] = prev[DEC_S_TONE_HIST + 2];
        b25_copy16_batch3(prev + DEC_S_TONE_HIST, src, 2);
    }

    if ((int16_t)coarse >= 0x34) {
        prev[DEC_S_STATUS_AL6B1] = -1;
        return 4;
    }

    synth[DEC_P_CLASS] = 1;
    cur[DEC_P_CLASS] = 1;
    synth[DEC_P_TAG] = 0xff;
    cur[DEC_P_TAG] = 0xff;

    widen = w32_sub((int32_t)alloc[5], 6);
    b25_set_pitch_params(cur, (int16_t)b25_shl16_al6b1(coarse, widen));

    b25_unpack_nibbles(hbits, cur[DEC_P_L], nib_tbl);
    for (i = 0; i < 56; i++) {
        hbits_i[i] = (int16_t)hbits[i];
    }
    b25_pack_imbe_frame(bits, cur[DEC_P_L], hbits_i, alloc);

    pitch = (uint32_t)b25_read_field_i16(bits, &pos, alloc[5]);
    b25_set_pitch_params(cur, (int16_t)pitch);

    triads = b25_triads_capped(cur[DEC_P_L]);
    vfield = b25_read_field_i16(bits, &pos, triads);

    vnarrow = (int32_t)(int16_t)vfield;
    if (triads > 8) {
        vnarrow >>= (uint32_t)((int32_t)triads - 8) & 0x1f;
    }
    prev[DEC_S_STATUS_AL6B1] = (int16_t)(uint16_t)w32_add((int32_t)(pitch << 8), vnarrow);

    mask = vfield;

    nb = triads;
    if (vfield == 0) {
        int16_t track = prev[DEC_S_TRACK];
        uint16_t below = w16u_sub((uint16_t)pitch, 0x80);
        int suspicious;
        idx = (track > 0 || below > 0x37) ? 0x38 : (int32_t)below;

        suspicious = (((uint8_t)pitch) & 0x7c) == 0x18
            || ((int16_t)(uint16_t)pitch >= 0x80 && (int16_t)(uint16_t)pitch < 0xb8);
        if (suspicious) {
            if (track > -3) {
                prev[DEC_S_TRACK] = w16_sub(track, 1);
            }
        } else if (track < 10) {
            prev[DEC_S_TRACK] = w16_add(track, 1);
        }
    } else if (triads > 8) {
        uint32_t folded = b25_map_voicing_bits((int16_t)vfield, (int32_t)triads, cur[DEC_P_L]);
        idx = w32_add((int32_t)(uint16_t)folded, 0x38);
        nb = 8;
    } else {
        idx = w32_add((int32_t)vfield, 0x38);
    }

    dibits = (uint32_t)(uint16_t)b25_gather_dibits((int32_t)b25_get32_al6b1(prev, DEC_P_VOICE));
    pattern = b25_voicing_pattern((int16_t)idx, dibits, nb);

    {
        uint32_t m_synth = b25_get32_al6b1(synth, DEC_P_VOICE);
        uint32_t m_cur = b25_get32_al6b1(cur, DEC_P_VOICE);
        b25_expand_dibits(&m_synth, &m_cur, pattern);
        b25_put32_al4b1(synth, DEC_P_VOICE, m_synth);
        b25_put32_al4b1(cur, DEC_P_VOICE, m_cur);
    }

    synth_mask = b25_get32_al6b1(synth, DEC_P_VOICE);
    b25_voiced_offset(&synth[DEC_P_OFFS], synth_mask, prev[DEC_P_OFFS], b25_get32_al6b1(prev, DEC_P_VOICE), voffs);
    synth_offs = synth[DEC_P_OFFS];
    cur_mask = b25_get32_al6b1(cur, DEC_P_VOICE);
    b25_voiced_offset(&cur[DEC_P_OFFS], cur_mask, synth_offs, synth_mask, voffs);

    b25_code_log_mags_with_voicing(prev, cur, (int16_t)mask, hbits, bits, &pos, 0, gain_book, weight, loading, blob, blob);

    b25_voicing_decision(cur, synth, prev, &mask, ctrl[DEC_C_THRESH], ctrl[DEC_C_FRAMES], ctrl + DEC_C_CFG, mant, exp);

    b25_suppress_spectral_peaks(cur, prev, ctrl[DEC_C_FRAMES], ctrl[DEC_C_SUBMODE]);

    {
        int16_t prev_code = prev[DEC_P_TAG];
        int16_t hist[3];
        memcpy(hist, prev + DEC_S_TONE_HIST, 3 * sizeof(int16_t));
        b25_detect_tone_frame(cur, prev_code, hist, blob, codewords);
    }

    cur[DEC_P_CLASS] = 1;
    b25_select_repeat_params(synth, cur, prev);
    b25_copy_params_batch2(prev, cur, (int16_t)mask);
    return 0;
}

/* ======================= from aL7b1 ======================= */

/* 0x1030b870 - decode one frame's payload with whichever codec is active */
int16_t b25_decode_frame(int16_t *cur, int16_t *synth, int16_t *prev, int16_t *bits, int16_t rate, int16_t voffs, const int16_t *ctrl, int16_t *alloc, int16_t mant, int16_t exp, int16_t allow_high, const b25_frame_tables *t)
{
    if (alloc[0] == 0) {
        return b25_decode_frame_imbe(cur, synth, prev, bits, voffs, ctrl, alloc, mant, exp, t->nibbles, t->gain64, t->hoc_weight, t->hoc_loading, t->codewords, t->blob);
    } else {
        return b25_decode_frame_ambe(cur, synth, prev, bits, rate, ctrl, alloc, allow_high, t->submode, t->voicing_patterns, t->gain32, t->block_lens, t->prba9, t->prba7, t->coef_books, t->blob);
    }
}

/* ======================= from aL8b1 ======================= */

/* 0x1030d5d0 - decode one 20 ms frame's parameters and pick the disposition */
int16_t b25_decode_frame_params(int16_t *bits, int16_t rate, uint16_t flags, int16_t voffs, int16_t *cur, int16_t *synth, int16_t *state, const b25_frame_tables *t)
{
    int hard_fail = 0;
    int soft_fail = 0;
    int16_t sym;
    int32_t substituted;
    int has_tone = 0;
    int16_t tone_unit = 0, tone_level = 0;
    size_t tmpl;

    flags = (uint16_t)(flags | (uint16_t)state[DEC_S_CTRL_FLAGS]);
    state[DEC_S_STATUS_AL8B1] = w16_mul(state[DEC_S_STATUS_AL8B1], 2);

    if ((flags & 0xc) == 0) {
        int16_t alloc[13];
        int16_t mant, exp;
        uint16_t status;
        memcpy(alloc, state + DEC_S_ALLOC, 13 * sizeof(int16_t));
        mant = state[DEC_S_MANT];
        exp = state[DEC_S_EXP];
        status = (uint16_t)b25_decode_frame(cur, synth, state + DEC_S_PREV_AL8B1, bits, rate, voffs, state + DEC_S_CTRL, alloc, mant, exp, (int16_t)(flags & 0x80), t);
        memcpy(state + DEC_S_ALLOC, alloc, 13 * sizeof(int16_t));
        if (status != 0) {
            if ((status & 8) == 0) {
                soft_fail = 1;
            } else {
                hard_fail = 1;
            }
        }
    }

    sym = cur[1];
    substituted = b25_tone_seq_track(&sym, state + DEC_S_TONE_SEQ, 0);

    if ((flags & 0x4000) != 0) {
        has_tone = 1;
        tone_unit = state[DEC_S_TONE_RATE];
        tone_level = state[DEC_S_TONE_LEVEL];
    } else if (substituted != 0) {
        int16_t unit = sym;
        int32_t cls = (int32_t)b25_frame_size_class(unit);
        int16_t hi = (int16_t)(uint16_t)b25_band_count_for_mode(cls, unit, 1, t->blob);
        int16_t lo = (int16_t)(uint16_t)b25_band_count_for_mode(cls, unit, 0, t->blob);
        int32_t a = (int32_t)cur[(size_t)(8 + (int32_t)hi)];
        int32_t b = (int32_t)cur[(size_t)(8 + (int32_t)lo)];
        has_tone = 1;
        tone_unit = unit;
        tone_level = (int16_t)((b + a) >> 1);
    }

    if (has_tone) {
        if (b25_pitch_config_init(cur, tone_unit, tone_level, tone_level, 0, t->blob) == 0) {
            b25_copy_params_reset_phase(synth, cur);
            return b25_finish_decoded(state, DEC_R_TONE);
        }
    }

    if ((flags & 8) != 0) {
        tmpl = DEC_S_TEMPLATE_GIVEUP;
    } else {
        if ((flags & 4) == 0) {
            if (hard_fail) {
                b25_state_copy(cur, synth, state, DEC_S_TEMPLATE_GIVEUP);
                return b25_finish_undecoded(state);
            }
            if (!soft_fail) {
                switch (cur[0]) {
                case 1: {
                    int16_t r;
                    if (state[DEC_S_ALLOC] == 0 && cur[1] != 0xff) {
                        r = DEC_R_TONE;
                    } else {
                        r = DEC_R_SPEECH;
                    }
                    return b25_finish_decoded(state, r);
                }
                case 2:
                    return b25_finish_decoded(state, DEC_R_SILENCE);
                case 3:
                    return b25_finish_decoded(state, DEC_R_TONE);
                default:
                    break;
                }
            }
        }
        if (((uint8_t)(uint16_t)state[DEC_S_STATUS_AL8B1] & 0xe) == 0xe) {
            tmpl = DEC_S_TEMPLATE_GIVEUP;
        } else {
            tmpl = DEC_S_TEMPLATE_HOLD;
        }
    }

    b25_state_copy(cur, synth, state, tmpl);
    return b25_finish_undecoded(state);
}

/* load a template into cur and synth, mark the frame undecoded */
void b25_state_copy(int16_t *cur, int16_t *synth, int16_t *state, size_t at)
{
    b25_copy_params_reset_phase(cur, state + at);
    b25_copy_params_reset_phase(synth, cur);
    state[DEC_S_STATUS_AL8B1] = (int16_t)(state[DEC_S_STATUS_AL8B1] | 1);
}

int16_t b25_finish_decoded(int16_t *state, int16_t ret)
{
    state[DEC_S_CLEARED] = 0;
    b25_bump_frame_counter(state + DEC_S_PREV_AL8B1);
    return ret;
}

int16_t b25_finish_undecoded(int16_t *state)
{
    b25_bump_frame_counter(state + DEC_S_PREV_AL8B1);
    return DEC_R_REPEAT;
}

/* ======================= from aL9b1 ======================= */

int16_t b25_clamp_half_frame(int16_t n)
{
    if (n > 0x54) {
        return 0x54;
    } else if (n < 0x4c) {
        return 0x4c;
    } else {
        return n;
    }
}

/* 0x10309dd0 - dec_synth(pcm, n, bits, rate, flags, half, state) */
int16_t b25_dec_synth(int16_t *pcm, int16_t n, int16_t *bits, int16_t rate, uint16_t flags, int16_t half, int16_t *state, const b25_dec_tables *t)
{
    int16_t frame[DEC_PARAM_WORDS];
    n = b25_clamp_half_frame(n);
    state[DEC_S_NOMINAL_N] = 0x50;

    memcpy(frame, state + DEC_S_FRAME_PARAMS, DEC_PARAM_WORDS * sizeof(int16_t));

    if (half == 0) {
        int16_t scratch[DEC_PARAM_WORDS] = {0};
        int16_t ret = b25_decode_frame_params(bits, rate, flags, n, frame, scratch, state, &t->frame);
        memcpy(state + DEC_S_FRAME_PARAMS, frame, DEC_PARAM_WORDS * sizeof(int16_t));
        b25_dec_synth_subframe(scratch, pcm, n, 0, state, t->hilbert, t->frame.blob, t->sqrt_tbl);
        return ret;
    } else {
        b25_dec_synth_subframe(frame, pcm, n, half, state, t->hilbert, t->frame.blob, t->sqrt_tbl);
        memcpy(state + DEC_S_FRAME_PARAMS, frame, DEC_PARAM_WORDS * sizeof(int16_t));
        return 0;
    }
}
