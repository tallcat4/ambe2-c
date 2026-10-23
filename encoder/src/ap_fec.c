/* AMBE+ channel coding of the AMBE-3000R: the encoding half of decoder/src/ambeplus_fec.c.
 *
 * Speech bits s[0..K) in serial (priority) order. With FEC, the first n bits are coded with the K=7, rate 1/2
 * convolutional code (171, 133 octal), zero-terminated, the remaining K-n bits follow uncoded:
 *   serial = conv(s[0..n)) ++ s[n..K)          (2(n+6) + K - n = N channel bits)
 * The channel frame is the serial stream written column by column into 12 rows (the first N % 12 rows one bit
 * longer) and read out row by row, MSB first. FEC-less rates use the same interleaver on s. */
#include <string.h>

#include "ap_internal.h"

#define AP_ROWS 12

static void interleave_positions(int nch, uint8_t *pos) {
    int width[AP_ROWS], start[AP_ROWS], r, c, i = 0, wide = nch % AP_ROWS;
    for (r = 0; r < AP_ROWS; r++) width[r] = nch / AP_ROWS + (r < wide ? 1 : 0);
    for (r = 0, start[0] = 0; r + 1 < AP_ROWS; r++) start[r + 1] = start[r] + width[r];
    for (c = 0; c < nch / AP_ROWS + 1; c++)
        for (r = 0; r < AP_ROWS; r++)
            if (c < width[r]) pos[i++] = (uint8_t)(start[r] + c);
}

static int parity7(unsigned v) {
    v &= 0x7f;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (int)(v & 1);
}

/* register bit k = input at delay k; generators 171 (delays 0,1,2,3,6) and 133 (delays 0,2,3,5,6) */
static void conv_encode(const uint8_t *u, int n, uint8_t *out) {
    unsigned reg = 0;
    int t;
    for (t = 0; t < n + 6; t++) {
        reg = ((reg << 1) | (t < n ? (u[t] & 1u) : 0u)) & 0x7f;
        out[2 * t] = (uint8_t)parity7(reg & 0x4f);
        out[2 * t + 1] = (uint8_t)parity7(reg & 0x6d);
    }
}

void ap_channel_encode(const uint8_t *speech, int nch, int K, int n, uint8_t *frame) {
    uint8_t pos[192], serial[192], channel[192];
    int i, m = 0;
    interleave_positions(nch, pos);
    if (n > 0) {
        conv_encode(speech, n, serial);
        m = 2 * (n + 6);
    }
    for (i = n > 0 ? n : 0; i < K; i++) serial[m++] = (uint8_t)(speech[i] & 1);
    for (i = 0; i < nch; i++) channel[pos[i]] = serial[i];
    memset(frame, 0, (size_t)(nch + 7) / 8);
    for (i = 0; i < nch; i++) frame[i >> 3] |= (uint8_t)(channel[i] << (7 - (i & 7)));
}
