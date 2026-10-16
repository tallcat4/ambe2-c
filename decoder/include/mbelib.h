/* mbelib-compatible API on top of blip25c (AMBE+2 half rate only).
 *
 * Drop-in replacement for the AMBE+2 3600x2450 / 2450 functions of mbelib 1.3 (DSD and similar applications):
 * same names, arguments, ambe_fr / ambe_d bit layouts, error counts, err_str characters and output scale
 * (float = short / 7, short clipped to +-32760). Synthesis is blip25's instead of mbelib's.
 *
 * Differences from mbelib:
 *  - The decoder state lives in cur_mp (mbe_parms now embeds a blip25_decoder); prev_mp / prev_mp_enhanced are
 *    accepted and initialised but not used. Use one set of mbe_parms per stream, as with mbelib.
 *  - uvquality is ignored. The mbelib parameter fields (w0, L, Vl, Ml, ...) are not filled in; repeat is.
 *  - The C0 codeword is decoded as extended Golay [24,12]: 4 errors are detected and the frame is concealed.
 *  - Only the AMBE+2 half-rate functions below exist (no IMBE, no D-STAR AMBE 3600x2400).
 * Copyright notice of the mbelib API this mirrors (ISC): Copyright (C) 2010 mbelib Author.
 */
#ifndef _MBELIB_H
#define _MBELIB_H

#include "blip25.h"

#define MBELIB_VERSION "1.3.0-blip25c"

struct mbe_parameters {
    float w0;
    int L;
    int K;
    int Vl[57];
    float Ml[57];
    float log2Ml[57];
    float PHIl[57];
    float PSIl[57];
    float gamma;
    int un;
    int repeat;
    blip25_decoder b25; /* blip25c decoder state (the stream's state is kept in cur_mp) */
};

typedef struct mbe_parameters mbe_parms;

#ifdef __cplusplus
extern "C" {
#endif

void mbe_printVersion(char *str);
void mbe_initMbeParms(mbe_parms *cur_mp, mbe_parms *prev_mp, mbe_parms *prev_mp_enhanced);

int mbe_eccAmbe3600x2450C0(char ambe_fr[4][24]);
void mbe_demodulateAmbe3600x2450Data(char ambe_fr[4][24]);
int mbe_eccAmbe3600x2450Data(char ambe_fr[4][24], char *ambe_d);

void mbe_processAmbe2450Dataf(float *aout_buf, int *errs, int *errs2, char *err_str, char ambe_d[49], mbe_parms *cur_mp,
                              mbe_parms *prev_mp, mbe_parms *prev_mp_enhanced, int uvquality);
void mbe_processAmbe2450Data(short *aout_buf, int *errs, int *errs2, char *err_str, char ambe_d[49], mbe_parms *cur_mp,
                             mbe_parms *prev_mp, mbe_parms *prev_mp_enhanced, int uvquality);
void mbe_processAmbe3600x2450Framef(float *aout_buf, int *errs, int *errs2, char *err_str, char ambe_fr[4][24],
                                    char ambe_d[49], mbe_parms *cur_mp, mbe_parms *prev_mp,
                                    mbe_parms *prev_mp_enhanced, int uvquality);
void mbe_processAmbe3600x2450Frame(short *aout_buf, int *errs, int *errs2, char *err_str, char ambe_fr[4][24],
                                   char ambe_d[49], mbe_parms *cur_mp, mbe_parms *prev_mp,
                                   mbe_parms *prev_mp_enhanced, int uvquality);

void mbe_synthesizeSilencef(float *aout_buf);
void mbe_synthesizeSilence(short *aout_buf);
void mbe_floattoshort(float *float_buf, short *aout_buf);

#ifdef __cplusplus
}
#endif
#endif
