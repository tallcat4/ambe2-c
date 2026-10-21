/* AMBE+ (AMBE-2000 family) rates of the AMBE-3000R on the blip25 synthesis engine. */
#ifndef AMBEPLUS_H
#define AMBEPLUS_H

#include <stdint.h>

/* rate argument of b25_dec_synth that selects the AMBE+ 2400 bps frame decoder */
#define B25_RATE_AMBEPLUS2400 ((int16_t)0x2400)
#define B25_RATE_AMBEPLUS2000 ((int16_t)0x2000) /* RATET 31: 2400 bps frame with the dropped LSBs set to 0 */
#define B25_RATE_AMBEPLUS4000 ((int16_t)0x4000) /* RATET 17 / 26: 80 speech bits */

extern const int16_t AP_GAIN64[64];
extern const int16_t AP_PRBA24[1536];
extern const int16_t AP_PRBA58[512];
extern const int16_t AP_HOC[256];
extern const uint32_t AP_VOICING[16];
extern const int16_t AP4_HOC[512];
extern const int16_t AP4_L2_GAIN[8];
extern const int16_t AP4_L2_VQ[1280];
extern const int16_t AP4_L2_GAIN_B[8];
extern const int16_t AP4_L2_SCALE[2];

extern const int16_t AP4_INTERP[8];

int16_t b25_decode_frame_ambeplus(int16_t *cur, int16_t *synth, int16_t *prev, const int16_t *bits, const int16_t *ctrl,
                                  int16_t allow_high, int16_t rate, const int16_t *block_lens, const int16_t *cos_tbl);

/* channel coding (ambeplus_fec.c) */
void ambeplus_interleave_positions(int N, uint8_t *pos);
void ambeplus_conv_encode(const uint8_t *u, int n, uint8_t *out);
void ambeplus_viterbi(const int16_t *soft, int n, uint8_t *u);
int ambeplus_fec_decode(const int16_t *soft_channel, int N, int K, int n, uint8_t *speech);
void ambeplus_fec_encode(const uint8_t *speech, int N, int K, int n, uint8_t *channel);

#endif
