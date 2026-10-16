#ifndef B25_DSP_B_H
#define B25_DSP_B_H
/* constants of part dsp_b (dsp.rs lines 2208-end, incl. the inner `pub mod tables`) */

/* dsp.rs top level */
extern const uint8_t DSP_TONE_SEQ[0x25];
extern const int16_t DSP_RECIP_N[57];

/* dsp.rs `pub mod tables` (re-exports: tables::COS512 = TB_COS512, tables::DFT32_TW = TB_TWIDDLE32) */
#define DSPT_COS512 TB_COS512
#define DSPT_DFT32_TW TB_TWIDDLE32
extern const int16_t DSPT_BIT_ALLOC[48];
extern const uint32_t DSPT_VOICING_LUT[56];
extern const int16_t DSPT_BITREV8[5];
extern const int16_t DSPT_BITREV16[13];
extern const int16_t DSPT_BITREV32[25];
extern const int16_t DSPT_BITREV64[57];
extern const int16_t DSPT_BITREV128[113];

#endif
