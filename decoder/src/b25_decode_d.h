#ifndef B25_DECODE_D_H
#define B25_DECODE_D_H
/* constants of part decode_d (decode.rs lines 2845-end) */

extern const size_t DEC_COEF_BOOK_OFF[4];
extern const int16_t DEC_COEF_BOOK_BITS[4];

#define DEC_S_RESON (0x00)
#define DEC_S_VOICED (0x0c)
#define DEC_S_SLOPE (0xb9)
#define DEC_S_PREV_AL5B1 (0x238)
#define DEC_S_TONE (0x27c)
#define DEC_S_NOISE (0x324)
#define DEC_S_PITCH_MSG (0x3d0)
#define DEC_S_BYPASS (0x3dd)
#define DEC_S_LEVEL (0x3df)
#define DEC_S_SHAPE_LEVEL (0x3e0)
#define DEC_S_SHAPE_EXP (0x3e1)
#define DEC_TAG_WORDS (0x38)

#define DEC_P_CLASS (0)
#define DEC_P_TAG (1)
#define DEC_P_L (2)
#define DEC_P_VOICE (4)
#define DEC_P_W0 (6)
#define DEC_P_OFFS (7)
#define DEC_S_TRACK (0x44)
#define DEC_S_TONE_HIST (0x46)
#define DEC_S_STATUS_AL6B1 (0x48)
#define DEC_C_THRESH (0x10)
#define DEC_C_CFG (0x11)
#define DEC_C_SUBMODE (0x12)
#define DEC_C_FRAMES (0x18)

#define DEC_S_TEMPLATE_HOLD (0x470 / 2)
#define DEC_S_TEMPLATE_GIVEUP (0x4f8 / 2)
#define DEC_S_PREV_AL8B1 (0x580 / 2)
#define DEC_S_CTRL (0x614 / 2)
#define DEC_S_CTRL_FLAGS (0x646 / 2)
#define DEC_S_TONE_RATE (0x79c / 2)
#define DEC_S_TONE_LEVEL (0x79e / 2)
#define DEC_S_ALLOC (0x7a0 / 2)
#define DEC_S_STATUS_AL8B1 (0x7ba / 2)
#define DEC_S_CLEARED (0x7bc / 2)
#define DEC_S_MANT (0x7c0 / 2)
#define DEC_S_EXP (0x7c2 / 2)
#define DEC_S_TONE_SEQ (0x7c4 / 2)
#define DEC_CTRL_WORDS (0x34 / 2)
#define DEC_R_SPEECH ((int16_t)2)
#define DEC_R_SILENCE ((int16_t)0)
#define DEC_R_TONE ((int16_t)-0x7ffe) /* 0x8002u16 as i16 */
#define DEC_R_REPEAT ((int16_t)0x20)

#define DEC_S_FRAME_PARAMS (0x3e8 / 2)
#define DEC_S_NOMINAL_N (0x170 / 2)
#define DEC_PARAM_WORDS (0x44)

#endif
