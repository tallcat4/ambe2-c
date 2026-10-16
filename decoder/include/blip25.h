/* blip25c - C port of blip25-vocoder's AMBE+2 half-rate (2450 bps) decoder.
 *
 * Bit-exact with blip25-vocoder 1.0 (Rust, MIT), whose synthesis is a fixed-point port of the reference software
 * vocoder: its output is within 0.77 dB (mel distance) of the AMBE-3000R chip's own decoder, unvoiced frames
 * sample-exact. Integer-only, no allocation, no libm; one decoder is ~2.5 KB of state. Not thread-safe per
 * instance; use one blip25_decoder per stream.
 *
 * Audio: 160 int16 samples at 8 kHz per 20 ms frame.
 * Frame formats:
 *   49-bit "ambe_d" order  - u0(12) u1(12) u2(11) u3(14), MSB first. The order of mbelib's / DSD's ambe_d[49]
 *                            and of .amb files; P25/DMR/NXDN payloads after FEC removal.
 *   7 bytes, ambe_d order  - the 49 bits packed MSB first, 7 pad bits.
 *   7 bytes, ThumbDV order - what the AMBE-3000R emits/accepts in rate 34 (2450 bps, no FEC).
 *   9 bytes, 3600x2450     - what the AMBE-3000R emits/accepts in rate 33 (FEC, 72 bits = 36 dibits, P25 Phase 2
 *                            Annex S interleave); verified against the chip.
 */
#ifndef BLIP25_H
#define BLIP25_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLIP25_FRAME_SAMPLES 160
#define BLIP25_SAMPLE_RATE 8000

/* frame status: error severity from the caller's FEC layer 0 (clean) .. 15 (worst), or BLIP25_LOST for a frame
 * that could not be recovered (the decoder repeats the previous parameters and fades out) */
#define BLIP25_LOST (-1)

/* disposition returned by the decode functions */
#define BLIP25_SILENCE 0x0000
#define BLIP25_SPEECH 0x0002
#define BLIP25_REPEAT 0x0020
#define BLIP25_TONE 0x8000

typedef struct blip25_decoder {
    int16_t state[0x400]; /* engine state (private) */
    int16_t bits[256];    /* payload scratch (private) */
} blip25_decoder;

/* Power-on state. Call before the first frame and to start a new stream. */
void blip25_init(blip25_decoder *dec);

/* One frame from 49 bits (one bit per byte, value 0/1) in ambe_d order. status: 0..15 or BLIP25_LOST. */
int blip25_decode_bits(blip25_decoder *dec, const uint8_t bits[49], int status, int16_t pcm[160]);

/* One clean frame, 7 bytes in ambe_d order (natural / mbelib / .amb). */
int blip25_decode_2450(blip25_decoder *dec, const uint8_t frame[7], int16_t pcm[160]);

/* One clean frame, 7 bytes in AMBE-3000R / ThumbDV rate-34 order. */
int blip25_decode_2450_thumbdv(blip25_decoder *dec, const uint8_t frame[7], int16_t pcm[160]);

/* One 9-byte AMBE-3000R rate-33 (3600x2450 with FEC) frame: Golay decoding, PN demodulation, then synthesis.
 * The total number of corrected bit errors is stored in *errors (may be NULL); a frame whose leading codeword is
 * uncorrectable is treated as lost. */
int blip25_decode_3600(blip25_decoder *dec, const uint8_t frame[9], int16_t pcm[160], int *errors);

/* One 3600x2450 frame from 72 soft decisions (log-likelihood ratios), in transmission order (llr[2k] = high bit of
 * dibit k, llr[2k+1] = low bit): sign = hard decision (> 0 means 1), magnitude = confidence. Chase-II soft Golay
 * decoding (about 2 dB coding gain over hard slicing), then the recovered bits are synthesised as a clean frame -
 * exactly as blip25-vocoder's Vocoder::decode_soft. */
int blip25_decode_3600_soft(blip25_decoder *dec, const int8_t llr[72], int16_t pcm[160]);

/* A frame that was not received at all: repeat and fade (concealment). */
int blip25_decode_lost(blip25_decoder *dec, int16_t pcm[160]);

/* ---- bit-format helpers */

/* 3600x2450 FEC only: 9-byte rate-33 frame -> 49 bits (ambe_d order). errors[4] (may be NULL) receives the
 * per-codeword corrections (c0: 0..3, or 255 if uncorrectable; c1: 0..3; c2, c3: 0). Returns the total number of
 * corrected errors, or -1 if c0 was uncorrectable. */
int blip25_fec_3600(const uint8_t frame[9], uint8_t bits[49], uint8_t errors[4]);

/* Soft-decision version of blip25_fec_3600 (72 LLRs as for blip25_decode_3600_soft). */
int blip25_fec_3600_soft(const int8_t llr[72], uint8_t bits[49], uint8_t errors[4]);

/* The same from four already-deinterleaved codewords c0 (24 bits), c1 (23), c2 (11), c3 (14), LSB = last bit. */
int blip25_fec_codewords(const uint32_t c[4], uint8_t bits[49], uint8_t errors[4]);

/* 7-byte ThumbDV (rate 34) order <-> 49 bits in ambe_d order. */
void blip25_thumbdv_to_bits(const uint8_t frame[7], uint8_t bits[49]);
void blip25_bits_to_thumbdv(const uint8_t bits[49], uint8_t frame[7]);

#ifdef __cplusplus
}
#endif
#endif
