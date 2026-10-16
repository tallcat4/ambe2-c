#ifndef B25_DECODE_C_H
#define B25_DECODE_C_H
/* constants of part decode_c (decode.rs lines 1-2844) */
#define DEC_T_STEP ((0x1037fd88 - 0x1037f978) / 2) /* 520 */
#define DEC_T_DEV ((0x1037fe60 - 0x1037f978) / 2)  /* 628 */
extern const int16_t DEC_HOC_SCALE[5];
extern const int16_t DEC_HOC_STEP[13];
#endif
