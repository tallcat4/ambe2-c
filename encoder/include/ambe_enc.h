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

/* 7-byte rate-34 frame -> 49 bits (one per byte) in ambe_d order (mbelib / DSD / blip25c) */
void ambe_enc_to_ambe_d(const uint8_t frame[7], uint8_t bits[49]);

#ifdef __cplusplus
}
#endif
#endif
