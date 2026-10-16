/* blip25c public API: engine driver, half-rate FEC (blip25-vocoder src/halfrate/frame.rs, src/fec.rs) and bit
 * formats. */
#include "blip25.h"
#include "b25_internal.h"

/* ---- engine driver (blip25-vocoder src/engine/mod.rs Decoder) */
static const b25_dec_tables DEC_TABLES = {
    {TB_SUBMODE, TB_VOICING_PATTERNS, TB_GAIN32, TB_BLOCK_LENS, TB_PRBA9, TB_PRBA7, TB_HOC_A, TB_NIBBLES, TB_GAIN64,
     TB_HOC_WEIGHT, TB_HOC_LOADING, TB_CODEWORDS, TB_BLOB},
    TB_HILBERT,
    TB_SQRT80,
};

void blip25_init(blip25_decoder *dec) {
    memset(dec, 0, sizeof(*dec));
    b25_dec_init(dec->state, 1); /* Mode::HalfRate */
}

int blip25_decode_bits(blip25_decoder *dec, const uint8_t bits[49], int status, int16_t pcm[160]) {
    int16_t ctrl[26] = {0};
    int i;
    int16_t disp;
    /* FrameStatus::to_ctrl: word 0x24/2 = error rate 0..15, word 0x32/2 = forcing word (bit 2 = lost) */
    if (status < 0) {
        ctrl[0x24 / 2] = 15;
        ctrl[0x32 / 2] = 4;
    } else {
        ctrl[0x24 / 2] = (int16_t)(status > 15 ? 15 : status);
    }
    memset(dec->bits, 0, sizeof(dec->bits));
    for (i = 0; i < 49; i++) dec->bits[i] = (int16_t)(bits[i] & 1);
    b25_dec_ctrl(dec->state, ctrl);
    disp = b25_dec_synth(pcm, 80, dec->bits, 0, 0, 0, dec->state, &DEC_TABLES);
    b25_dec_synth(pcm + 80, 80, dec->bits, 0, 0, 1, dec->state, &DEC_TABLES);
    return (int)(uint16_t)disp;
}

int blip25_decode_2450(blip25_decoder *dec, const uint8_t frame[7], int16_t pcm[160]) {
    uint8_t bits[49];
    int i;
    for (i = 0; i < 49; i++) bits[i] = (uint8_t)((frame[i >> 3] >> (7 - (i & 7))) & 1);
    return blip25_decode_bits(dec, bits, 0, pcm);
}

int blip25_decode_2450_thumbdv(blip25_decoder *dec, const uint8_t frame[7], int16_t pcm[160]) {
    uint8_t bits[49];
    blip25_thumbdv_to_bits(frame, bits);
    return blip25_decode_bits(dec, bits, 0, pcm);
}

int blip25_decode_lost(blip25_decoder *dec, int16_t pcm[160]) {
    static const uint8_t zero[49] = {0};
    return blip25_decode_bits(dec, zero, BLIP25_LOST, pcm);
}

int blip25_decode_3600(blip25_decoder *dec, const uint8_t frame[9], int16_t pcm[160], int *errors) {
    uint8_t bits[49];
    int e = blip25_fec_3600(frame, bits, NULL);
    if (errors) *errors = e < 0 ? 4 : e;
    /* Vocoder::decode at HalfRate3600x2450: uncorrectable c0 -> LOST, else severity = total corrected errors */
    return blip25_decode_bits(dec, bits, e < 0 ? BLIP25_LOST : e, pcm);
}

/* ---- half-rate FEC */
extern const uint32_t b25_golay23_gen[12];
extern const uint32_t b25_golay23_syndrome_table[2048];

static uint32_t golay23_encode(uint16_t info) {
    uint32_t cw = 0;
    int i;
    for (i = 0; i < 12; i++)
        if ((info >> (11 - i)) & 1) cw ^= b25_golay23_gen[i];
    return cw;
}

static uint16_t golay23_syndrome(uint32_t cw) {
    uint32_t info = (cw >> 11) & 0xFFF, rec = 0;
    int i;
    for (i = 0; i < 12; i++)
        if ((info >> (11 - i)) & 1) rec ^= b25_golay23_gen[i] & 0x7FF;
    return (uint16_t)((cw & 0x7FF) ^ rec);
}

static uint16_t golay23_decode(uint32_t codeword, uint8_t *errors) {
    uint32_t cw = codeword & 0x7FFFFF;
    uint32_t pat = b25_golay23_syndrome_table[golay23_syndrome(cw)];
    *errors = (uint8_t)popcnt32(pat);
    return (uint16_t)(((cw ^ pat) >> 11) & 0xFFF);
}

static uint16_t golay24_decode(uint32_t codeword, uint8_t *errors) {
    uint32_t cw = codeword & 0xFFFFFF;
    uint8_t e23;
    uint16_t info = golay23_decode((cw >> 1) & 0x7FFFFF, &e23);
    int match = (popcnt32(golay23_encode(info)) & 1) == (cw & 1);
    *errors = (e23 == 3 && !match) ? 255 : match ? e23 : (uint8_t)(e23 + 1);
    return info;
}

int blip25_fec_codewords(const uint32_t c[4], uint8_t bits[49], uint8_t errors[4]) {
    uint8_t e[4] = {0, 0, 0, 0};
    uint16_t u[4], pr = 0;
    uint32_t mask = 0;
    int i, k, n = 0;
    static const int widths[4] = {12, 12, 11, 14};
    u[0] = golay24_decode(c[0], &e[0]);
    /* PN sequence seeded by 16 * u0; bit k of the c1 mask = MSB of p_r(1 + k), first PN at bit 22 */
    pr = (uint16_t)((u[0] & 0x0FFF) * 16u);
    for (k = 0; k < 23; k++) {
        pr = (uint16_t)((173u * pr + 13849u) & 0xFFFFu);
        mask |= (uint32_t)(pr >> 15) << (22 - k);
    }
    u[1] = golay23_decode(c[1] ^ mask, &e[1]);
    u[2] = (uint16_t)(c[2] & 0x7FF);
    u[3] = (uint16_t)(c[3] & 0x3FFF);
    for (i = 0; i < 4; i++)
        for (k = widths[i] - 1; k >= 0; k--) bits[n++] = (uint8_t)((u[i] >> k) & 1);
    if (errors) memcpy(errors, e, 4);
    return e[0] == 255 ? -1 : e[0] + e[1];
}

/* Annex S (TIA-102.BABA-A): dibit -> (codeword, bit) for its high bit and its low bit */
static const uint8_t ANNEX_S[36][4] = {
    {0, 23, 0, 5},
    {1, 10, 2, 3},
    {0, 22, 0, 4},
    {1, 9, 2, 2},
    {0, 21, 0, 3},
    {1, 8, 2, 1},
    {0, 20, 0, 2},
    {1, 7, 2, 0},
    {0, 19, 0, 1},
    {1, 6, 3, 13},
    {0, 18, 0, 0},
    {1, 5, 3, 12},
    {0, 17, 1, 22},
    {1, 4, 3, 11},
    {0, 16, 1, 21},
    {1, 3, 3, 10},
    {0, 15, 1, 20},
    {1, 2, 3, 9},
    {0, 14, 1, 19},
    {1, 1, 3, 8},
    {0, 13, 1, 18},
    {1, 0, 3, 7},
    {0, 12, 1, 17},
    {2, 10, 3, 6},
    {0, 11, 1, 16},
    {2, 9, 3, 5},
    {0, 10, 1, 15},
    {2, 8, 3, 4},
    {0, 9, 1, 14},
    {2, 7, 3, 3},
    {0, 8, 1, 13},
    {2, 6, 3, 2},
    {0, 7, 1, 12},
    {2, 5, 3, 1},
    {0, 6, 1, 11},
    {2, 4, 3, 0}
};

int blip25_fec_3600(const uint8_t frame[9], uint8_t bits[49], uint8_t errors[4]) {
    uint32_t c[4] = {0, 0, 0, 0};
    int s;
    for (s = 0; s < 36; s++) {
        uint32_t hi = (frame[(2 * s) >> 3] >> (7 - ((2 * s) & 7))) & 1;
        uint32_t lo = (frame[(2 * s + 1) >> 3] >> (7 - ((2 * s + 1) & 7))) & 1;
        c[ANNEX_S[s][0]] |= hi << ANNEX_S[s][1];
        c[ANNEX_S[s][2]] |= lo << ANNEX_S[s][3];
    }
    return blip25_fec_codewords(c, bits, errors);
}

/* ---- soft decision (blip25-vocoder src/fec.rs Chase-II, src/halfrate/frame.rs decode_frame_soft) */
#define CHASE_DEPTH 6

static uint32_t golay24_encode(uint16_t info) {
    uint32_t cw23 = golay23_encode(info);
    return (cw23 << 1) | (popcnt32(cw23) & 1);
}

/* soft[0] = MSB; sign = hard bit (> 0 -> 1), magnitude = confidence */
static uint32_t soft_distance(const int8_t *soft, int n, uint32_t cand) {
    uint32_t d = 0;
    int i;
    for (i = 0; i < n; i++) {
        int cb = (int)((cand >> (n - 1 - i)) & 1), hard = soft[i] > 0;
        if (cb != hard) d += (uint32_t)(soft[i] < 0 ? -(int)soft[i] : soft[i]);
    }
    return d;
}

/* Chase-II over the CHASE_DEPTH least reliable positions (ties: lower position first), at most 3 flips;
 * returns the info of the candidate whose re-encoded codeword has the smallest soft distance (first wins). */
static uint16_t golay_decode_soft(const int8_t *soft, int n, uint8_t *errors) {
    int weak[CHASE_DEPTH], i, j, k;
    uint8_t used[24] = {0};
    uint32_t hard = 0, pattern, best_dist = 0xFFFFFFFFu;
    uint16_t best_info = 0;
    uint8_t best_err = 0;
    for (i = 0; i < n; i++) hard = (hard << 1) | (uint32_t)(soft[i] > 0);
    for (k = 0; k < CHASE_DEPTH; k++) {
        int bi = -1, bc = 256;
        for (j = 0; j < n; j++) {
            int c = soft[j] < 0 ? -(int)soft[j] : soft[j];
            if (!used[j] && c < bc) { bc = c; bi = j; }
        }
        weak[k] = bi;
        used[bi] = 1;
    }
    for (pattern = 0; pattern < (1u << CHASE_DEPTH); pattern++) {
        uint32_t cand = hard, re, dist;
        uint16_t info;
        uint8_t e;
        if (popcnt32(pattern) > 3) continue;
        for (k = 0; k < CHASE_DEPTH; k++)
            if ((pattern >> k) & 1) cand ^= 1u << (n - 1 - weak[k]);
        if (n == 24) { info = golay24_decode(cand, &e); re = golay24_encode(info); }
        else { info = golay23_decode(cand, &e); re = golay23_encode(info); }
        dist = soft_distance(soft, n, re);
        if (dist < best_dist) { best_dist = dist; best_info = info; best_err = e; }
    }
    *errors = best_err;
    return best_info;
}

int blip25_fec_3600_soft(const int8_t llr[72], uint8_t bits[49], uint8_t errors[4]) {
    int8_t c0[24], c1[23], c2[11], c3[14];
    int8_t *vec[4] = {c0, c1, c2, c3};
    static const int width[4] = {24, 23, 11, 14};
    uint8_t e[4] = {0, 0, 0, 0};
    uint16_t u[4] = {0, 0, 0, 0}, pr;
    int s, i, k, n = 0;
    static const int widths[4] = {12, 12, 11, 14};
    for (s = 0; s < 36; s++) {   /* Annex S, MSB-first soft vectors */
        vec[ANNEX_S[s][0]][width[ANNEX_S[s][0]] - 1 - ANNEX_S[s][1]] = llr[2 * s];
        vec[ANNEX_S[s][2]][width[ANNEX_S[s][2]] - 1 - ANNEX_S[s][3]] = llr[2 * s + 1];
    }
    u[0] = golay_decode_soft(c0, 24, &e[0]);
    pr = (uint16_t)((u[0] & 0x0FFF) * 16u);
    for (k = 0; k < 23; k++) {          /* PN mask bit 22 - k flips soft bit k (saturating negation) */
        pr = (uint16_t)((173u * pr + 13849u) & 0xFFFFu);
        if (pr >> 15) c1[k] = (int8_t)(c1[k] == -128 ? 127 : -c1[k]);
    }
    u[1] = golay_decode_soft(c1, 23, &e[1]);
    for (i = 0; i < 11; i++) u[2] |= (uint16_t)((c2[i] > 0) << (10 - i));
    for (i = 0; i < 14; i++) u[3] |= (uint16_t)((c3[i] > 0) << (13 - i));
    for (i = 0; i < 4; i++)
        for (k = widths[i] - 1; k >= 0; k--) bits[n++] = (uint8_t)((u[i] >> k) & 1);
    if (errors) memcpy(errors, e, 4);
    return e[0] == 255 ? -1 : e[0] + e[1];
}

int blip25_decode_3600_soft(blip25_decoder *dec, const int8_t llr[72], int16_t pcm[160]) {
    uint8_t bits[49];
    blip25_fec_3600_soft(llr, bits, NULL);
    /* Vocoder::decode_soft re-encodes the recovered info as a clean frame and decodes that: status clean */
    return blip25_decode_bits(dec, bits, 0, pcm);
}

/* ---- ThumbDV (AMBE-3000R rate 34) bit order: raw bit i is ambe_d bit RAW_TO_AMBE_D[i] */
static const uint8_t RAW_TO_AMBE_D[49] = {0, 18, 36, 1, 19, 37, 2, 20, 38, 3, 21, 39, 4, 22, 40, 5, 23, 41, 6, 24, 42, 7, 25, 43, 8, 26, 44, 9, 27, 45, 10, 28, 46, 11, 29, 47, 12, 30, 48, 13, 31, 14, 32, 15, 33, 16, 34, 17, 35};

void blip25_thumbdv_to_bits(const uint8_t frame[7], uint8_t bits[49]) {
    int i;
    for (i = 0; i < 49; i++) bits[RAW_TO_AMBE_D[i]] = (uint8_t)((frame[i >> 3] >> (7 - (i & 7))) & 1);
}

void blip25_bits_to_thumbdv(const uint8_t bits[49], uint8_t frame[7]) {
    int i;
    memset(frame, 0, 7);
    for (i = 0; i < 49; i++) frame[i >> 3] |= (uint8_t)((bits[RAW_TO_AMBE_D[i]] & 1) << (7 - (i & 7)));
}
