#ifndef B25_DSP_A_H
#define B25_DSP_A_H
/* constants of part dsp_a (dsp.rs lines 1-2207) */
#define DSP_FRAME_COUNTER_WORD ((size_t)(0x86 / 2))
extern const int16_t DSP_GAIN_CODEBOOK[64];
extern const int16_t DSP_TWIDDLE[512];
extern const int16_t DSP_LOG2_C[6];
#endif
