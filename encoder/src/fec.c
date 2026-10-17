/* 3600x2450 FEC encoding (AMBE-3000R rate 33): the inverse of the decoder's blip25_fec_3600 (TIA-102.BABA-A
 * half-rate frame). The 49 bits u0(12) u1(12) u2(11) u3(14) become four codewords: c0 = extended Golay [24,12] of
 * u0, c1 = Golay [23,12] of u1 xor a PN sequence seeded by u0, c2 = u2 and c3 = u3 unprotected; then the Annex S
 * interleave spreads them over 36 dibits. Shared by all three encoder builds (integer only). */
#include "ambe_enc.h"

/* Golay [23,12], g(x) = x^11 + x^10 + x^6 + x^5 + x^4 + x^2 + 1: info in bits 22..11, parity in bits 10..0 */
static uint32_t golay23_encode(uint32_t info) {
    uint32_t r = info << 11;
    int i;
    for (i = 22; i >= 11; i--)
        if ((r >> i) & 1) r ^= 0xC75u << (i - 11);
    return (info << 11) | r;
}

/* Annex S: dibit -> (codeword, bit) for its high bit and its low bit (as in decoder/src/blip25.c) */
static const uint8_t ANNEX_S[36][4] = {
    {0, 23, 0, 5}, {1, 10, 2, 3}, {0, 22, 0, 4}, {1, 9, 2, 2}, {0, 21, 0, 3}, {1, 8, 2, 1},
    {0, 20, 0, 2}, {1, 7, 2, 0}, {0, 19, 0, 1}, {1, 6, 3, 13}, {0, 18, 0, 0}, {1, 5, 3, 12},
    {0, 17, 1, 22}, {1, 4, 3, 11}, {0, 16, 1, 21}, {1, 3, 3, 10}, {0, 15, 1, 20}, {1, 2, 3, 9},
    {0, 14, 1, 19}, {1, 1, 3, 8}, {0, 13, 1, 18}, {1, 0, 3, 7}, {0, 12, 1, 17}, {2, 10, 3, 6},
    {0, 11, 1, 16}, {2, 9, 3, 5}, {0, 10, 1, 15}, {2, 8, 3, 4}, {0, 9, 1, 14}, {2, 7, 3, 3},
    {0, 8, 1, 13}, {2, 6, 3, 2}, {0, 7, 1, 12}, {2, 5, 3, 1}, {0, 6, 1, 11}, {2, 4, 3, 0}};

void ambe_enc_bits_to_rate33(const uint8_t bits[49], uint8_t out[9]) {
    static const int widths[4] = {12, 12, 11, 14};
    uint32_t u[4] = {0, 0, 0, 0}, c[4], mask = 0, cw23;
    uint16_t pr;
    int i, k, n = 0, s;
    for (i = 0; i < 4; i++)
        for (k = 0; k < widths[i]; k++) u[i] = (u[i] << 1) | (bits[n++] & 1u);
    cw23 = golay23_encode(u[0]);
    for (k = 0, i = 0; k < 23; k++) i ^= (int)((cw23 >> k) & 1);
    c[0] = (cw23 << 1) | (uint32_t)i;   /* even overall parity */
    pr = (uint16_t)(u[0] * 16u);          /* PN sequence: bit 22 - k of the c1 mask = MSB of p_r(1 + k) */
    for (k = 0; k < 23; k++) {
        pr = (uint16_t)((173u * pr + 13849u) & 0xFFFFu);
        mask |= (uint32_t)(pr >> 15) << (22 - k);
    }
    c[1] = golay23_encode(u[1]) ^ mask;
    c[2] = u[2];
    c[3] = u[3];
    for (i = 0; i < 9; i++) out[i] = 0;
    for (s = 0; s < 36; s++) {
        out[(2 * s) >> 3] |= (uint8_t)(((c[ANNEX_S[s][0]] >> ANNEX_S[s][1]) & 1) << (7 - ((2 * s) & 7)));
        out[(2 * s + 1) >> 3] |= (uint8_t)(((c[ANNEX_S[s][2]] >> ANNEX_S[s][3]) & 1) << (7 - ((2 * s + 1) & 7)));
    }
}

void ambe_enc_to_rate33(const uint8_t frame[7], uint8_t out[9]) {
    uint8_t bits[49];
    ambe_enc_to_ambe_d(frame, bits);
    ambe_enc_bits_to_rate33(bits, out);
}
