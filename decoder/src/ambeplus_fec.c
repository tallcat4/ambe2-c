/* AMBE+ channel coding of the AMBE-3000R (RATET 17, 22, 26 ...), recovered from chip frames.
 *
 * Speech bits s[0..K) in serial (priority) order. With FEC, the first n bits are coded with the K=7, rate 1/2
 * convolutional code (171, 133 octal), zero-terminated, the remaining K-n bits follow uncoded:
 *   serial = conv(s[0..n)) ++ s[n..K)          (2(n+6) + K - n = N channel bits)
 * The channel frame is the serial stream written column by column into 12 rows (the first N % 12 rows one bit
 * longer) and read out row by row, MSB first. FEC-less rates use the same interleaver on s. */
#include "ambeplus.h"

#include <string.h>

#define AP_ROWS 12

void ambeplus_interleave_positions(int N, uint8_t *pos) {
    int width[AP_ROWS], start[AP_ROWS], r, c, i = 0, wide = N % AP_ROWS;
    for (r = 0; r < AP_ROWS; r++) width[r] = N / AP_ROWS + (r < wide ? 1 : 0);
    for (r = 0, start[0] = 0; r + 1 < AP_ROWS; r++) start[r + 1] = start[r] + width[r];
    for (c = 0; c < N / AP_ROWS + 1; c++)
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

/* register bit k = input at delay k; generator g as octal with its MSB = delay 0 */
static int conv_out(unsigned reg, int p) {
    static const unsigned TAPS[2] = {0x4f /* 171: delays 0,1,2,3,6 */, 0x6d /* 133: delays 0,2,3,5,6 */};
    return parity7(reg & TAPS[p]);
}

void ambeplus_conv_encode(const uint8_t *u, int n, uint8_t *out) {
    unsigned reg = 0;
    int t;
    for (t = 0; t < n + 6; t++) {
        reg = ((reg << 1) | (t < n ? (u[t] & 1u) : 0u)) & 0x7f;
        out[2 * t] = (uint8_t)conv_out(reg, 0);
        out[2 * t + 1] = (uint8_t)conv_out(reg, 1);
    }
}

/* soft[i] > 0: bit 1, magnitude = confidence. Maximum-likelihood, terminated trellis. n <= 64. */
void ambeplus_viterbi(const int16_t *soft, int n, uint8_t *u) {
    int32_t metric[64], next[64];
    uint64_t path[64], npath[64];
    int t, s, b;
    for (s = 0; s < 64; s++) metric[s] = s ? INT32_MAX / 2 : 0;
    memset(path, 0, sizeof(path));
    for (t = 0; t < n + 6; t++) {
        for (s = 0; s < 64; s++) next[s] = INT32_MAX / 2;
        for (s = 0; s < 64; s++) {
            if (metric[s] >= INT32_MAX / 2) continue;
            for (b = 0; b < (t < n ? 2 : 1); b++) {
                unsigned reg = ((unsigned)s << 1) | (unsigned)b;
                int ns = (int)(reg & 63), p;
                int32_t m = metric[s];
                for (p = 0; p < 2; p++) {
                    int16_t x = soft[2 * t + p];
                    int bit = conv_out(reg, p);
                    if ((x > 0) != bit) m += x < 0 ? -x : x;
                }
                if (m < next[ns]) {
                    next[ns] = m;
                    npath[ns] = t < 64 ? (path[s] | ((uint64_t)b << t)) : path[s];
                }
            }
        }
        memcpy(metric, next, sizeof(metric));
        memcpy(path, npath, sizeof(path));
    }
    for (t = 0; t < n; t++) u[t] = (uint8_t)((path[0] >> t) & 1);
}

int ambeplus_fec_decode(const int16_t *soft_channel, int N, int K, int n, uint8_t *speech) {
    uint8_t pos[192], coded[2 * (64 + 6)];
    int16_t serial[192];
    int i, errors = 0;
    ambeplus_interleave_positions(N, pos);
    for (i = 0; i < N; i++) serial[i] = soft_channel[pos[i]];
    if (n <= 0) {
        for (i = 0; i < K; i++) speech[i] = (uint8_t)(serial[i] > 0);
        return 0;
    }
    ambeplus_viterbi(serial, n, speech);
    ambeplus_conv_encode(speech, n, coded);
    for (i = 0; i < 2 * (n + 6); i++) errors += (serial[i] > 0) != coded[i];
    for (i = 0; i < K - n; i++) speech[n + i] = (uint8_t)(serial[2 * (n + 6) + i] > 0);
    return errors;
}

void ambeplus_fec_encode(const uint8_t *speech, int N, int K, int n, uint8_t *channel) {
    uint8_t pos[192], serial[192];
    int i, m = 0;
    ambeplus_interleave_positions(N, pos);
    if (n > 0) {
        ambeplus_conv_encode(speech, n, serial);
        m = 2 * (n + 6);
    }
    for (i = n > 0 ? n : 0; i < K; i++) serial[m++] = (uint8_t)(speech[i] & 1);
    for (i = 0; i < N; i++) channel[pos[i]] = serial[i];
}
