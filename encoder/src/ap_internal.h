/* AMBE+ rates of the encoder (ap_quant.c, ap_fec.c) */
#ifndef AP_INTERNAL_H
#define AP_INTERNAL_H

#include "enc_internal.h"
#include "ap_tables.h"

#define AP_2400 1 /* RATET 0, 22: 48 speech bits */
#define AP_2000 2 /* RATET 31: 40 speech bits (the 2400 bps fields without their LSBs) */
#define AP_4000 3 /* RATET 17, 26: 80 speech bits */

real ap_f0(int kind, int b0);
int ap_harmonics(int kind, int b0);
int ap_pitch_code(int kind, real f0);

/* c[0..8] = b0, b1, b2, b3, b4, hoc1..hoc4 as the 2400 / 4000 bps field values (2000 bps: 2400 bps values with
 * LSB 0); ap_quantize sets c[2..8] */
void ap_quantize(int kind, const real *y, int L, const q_state *s, int *c);
void ap_decode(int kind, const int *c, int L, const q_state *s, q_state *ns);
void ap_pack(int kind, const int *c, uint8_t *serial);
/* 4000 bps refinements r[0..7) = interpolation, layer-2 gain, layer-2 VQ, split VQ 67-70, 71-74, 75-78, bit 66 */
void ap_refine_neutral(int *r);
void ap_pack_refine(const int *r, uint8_t *serial);
real ap_half_f0(real f0_prev, int voiced_prev, real f0_cur, int voiced_cur);
int ap_half_harmonics(real f0);
void ap_refine(const q_state *prev, real f0_prev, const q_state *cur, real f0_cur, real fs, const real *th, int *r);

/* K speech bits (one per byte, serial order) -> nch-bit channel frame, MSB first; n bits convolutionally coded */
void ap_channel_encode(const uint8_t *speech, int nch, int K, int n, uint8_t *frame);

#endif
