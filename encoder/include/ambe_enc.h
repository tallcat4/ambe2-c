/* ambe-enc: AMBE+2 2450 bps (no FEC) encoder, bit-compatible with the AMBE-3000R (ThumbDV rate 34).
 *
 * Input: 8 kHz mono int16, 160 samples (20 ms) per call. Output: one frame of 49 bits in 7 bytes, AMBE-3000R
 * rate-34 bit order (use ambe_enc_to_ambe_d for the mbelib / DSD ambe_d order).
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

typedef struct ambe_encoder ambe_encoder;

/* size of the encoder state in bytes (for static allocation: e.g. a buffer of this size, suitably aligned) */
unsigned long ambe_enc_size(void);

/* initialise (or reset) an encoder in caller-provided memory of ambe_enc_size() bytes */
ambe_encoder *ambe_enc_init(void *mem);

/* push 160 samples; returns 1 and writes out[7] when a frame is ready, else 0 */
int ambe_enc_push(ambe_encoder *enc, const int16_t pcm[160], uint8_t out[7]);

/* end of stream: returns 1 and writes the last frame, 0 if nothing is pending. Re-init before reuse. */
int ambe_enc_flush(ambe_encoder *enc, uint8_t out[7]);

/* Background-noise mode (off by default; call right after ambe_enc_init): like the AMBE-3000R with ECMODE 0x0800,
 * frames that a VAD takes for silence or background noise are sent as b0 = 124 background-noise frames (b2 = noise
 * level, b3..b8 = its spectral shape) instead of speech frames. The VAD and the level / shape models were trained on
 * the chip's own ECMODE 0x0800 output; they agree with its frame decisions about 92% of the time. */
void ambe_enc_set_background(ambe_encoder *enc, int on);

/* ---- silence / background-noise frames (see README, "無音フレームの仕様")
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

#ifdef __cplusplus
}
#endif
#endif
