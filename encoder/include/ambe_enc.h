/* ambe-enc: AMBE+2 2450 bps encoder, bit-compatible with the AMBE-3000R (ThumbDV rate 34), and the AMBE+ rates of
 * the AMBE-3000R (ambe_enc_set_rate).
 *
 * Input: 8 kHz mono int16, 160 samples (20 ms) per call. Output: one frame per call; by default 49 bits in 7 bytes,
 * AMBE-3000R rate-34 bit order (use ambe_enc_to_ambe_d for the mbelib / DSD ambe_d order).
 * The encoder decides each frame one frame late (pitch tracking looks one frame ahead): ambe_enc_push returns the
 * frame of the PREVIOUS chunk (0 on the very first call) and ambe_enc_flush the last one. N chunks -> N frames.
 * The analysis was trained at an active speech level of about -26 dBFS. No allocation; one encoder per stream.
 */
#ifndef AMBE_ENC_H
#define AMBE_ENC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMBE_ENC_FRAME_SAMPLES 160
#define AMBE_ENC_MAX_BYTES 16 /* largest frame of any rate (RATET 26) */

typedef struct ambe_encoder ambe_encoder;

/* size of the encoder state in bytes (for static allocation: e.g. a buffer of this size, suitably aligned) */
unsigned long ambe_enc_size(void);

/* initialise (or reset) an encoder in caller-provided memory of ambe_enc_size() bytes */
ambe_encoder *ambe_enc_init(void *mem);

/* Output format (call right after ambe_enc_init): the AMBE-3000R rate index RATET. Returns the frame size in bytes,
 * 0 for an unsupported rate (the format is then unchanged).
 *   34  AMBE+2 2450 bps, no FEC (default)     7 bytes
 *   33  AMBE+2 3600x2450 (FEC)                 9 bytes
 *   31  AMBE+ 2000 bps, no FEC                 5 bytes
 *    0  AMBE+ 2400 bps, no FEC                 6 bytes
 *   22  AMBE+ 4000x2400 (2400 bps + FEC)      10 bytes
 *   17  AMBE+ 4000 bps, no FEC                10 bytes
 *   26  AMBE+ 6400x4000 (4000 bps + FEC)      16 bytes
 * Frames are the chip's channel frames, MSB first (the input of decoder/ambe2wav --rate N). The AMBE+ rates use the
 * same analysis with the AMBE+ parameters and codebooks; the background-noise mode is for RATET 34 / 33 only. */
int ambe_enc_set_rate(ambe_encoder *enc, int ratet);

/* push 160 samples; returns 1 and writes one frame to out (7 bytes by default, see ambe_enc_set_rate) when a frame
 * is ready, else 0 */
int ambe_enc_push(ambe_encoder *enc, const int16_t pcm[160], uint8_t *out);

/* end of stream: returns 1 and writes the last frame, 0 if nothing is pending. Re-init before reuse. */
int ambe_enc_flush(ambe_encoder *enc, uint8_t *out);

/* Background-noise mode (off by default; call right after ambe_enc_init): like the AMBE-3000R with ECMODE 0x0800,
 * frames that a VAD takes for silence or background noise are sent as b0 = 124 background-noise frames (b2 = noise
 * level, b3..b8 = its spectral shape) instead of speech frames. The VAD and the level / shape models were trained on
 * the chip's own ECMODE 0x0800 output; they agree with its frame decisions about 92% of the time. */
void ambe_enc_set_background(ambe_encoder *enc, int on);   /* ignored for the AMBE+ rates */

/* ---- silence / background-noise frames (see docs/encoder.md, "無音フレームの仕様")
 * The encoder itself codes digital silence (any constant input) the way the AMBE-3000R does with its default
 * settings: b0 = 119, b1 = 16, b2 = 0, which decodes to exact zero. The frames below are for applications that
 * want the special b0 = 124 frames instead (the AMBE-3000R decoder plays b0 in {124, 125} with b1 in 16..23 as
 * background noise whose level is set by b2; it never emits them with default settings). */

/* The DMR silence frame (b0..b8 = 124 16 1 52 79 18 14 12 1; about -136 dBFS on the AMBE-3000R decoder). */
extern const uint8_t AMBE_ENC_DMR_SILENCE_RATE34[7];   /* f7 4a d8 09 26 b4 00 */
extern const uint8_t AMBE_ENC_DMR_SILENCE_AMBE_D[7];   /* f8 01 a9 9f 8c e0 80 (mbelib / DSD order) */
extern const uint8_t AMBE_ENC_DMR_SILENCE_RATE33[9];   /* b9 e8 81 52 61 73 00 2a 6b (3600x2450 with FEC) */

/* A background-noise frame of level `gain` (b2 = 0..31; 0 = exact silence, 1 ~ -136 dBFS, 2 ~ -96, 16 ~ -79,
 * 31 ~ -58 dBFS on the AMBE-3000R decoder) with the DMR silence frame's spectral shape. Rate-34 order. */
void ambe_enc_background_frame(int gain, uint8_t out[7]);

/* 7-byte rate-34 frame -> 49 bits (one per byte) in ambe_d order (mbelib / DSD / blip25c) */
void ambe_enc_to_ambe_d(const uint8_t frame[7], uint8_t bits[49]);

/* ---- 3600x2450 FEC (AMBE-3000R rate 33; src/fec.c)
 * Golay [24,12] on u0, Golay [23,12] with PN modulation on u1, u2 and u3 unprotected, P25 Phase 2 Annex S
 * interleave: 72 bits = 36 dibits in 9 bytes, MSB first. The inverse of the decoder's blip25_fec_3600. */

/* 7-byte rate-34 frame -> 9-byte rate-33 frame */
void ambe_enc_to_rate33(const uint8_t frame[7], uint8_t out[9]);

/* 49 bits (one per byte) in ambe_d order -> 9-byte rate-33 frame */
void ambe_enc_bits_to_rate33(const uint8_t bits[49], uint8_t out[9]);

#ifdef __cplusplus
}
#endif
#endif
