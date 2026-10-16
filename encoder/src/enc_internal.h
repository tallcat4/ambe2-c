#ifndef ENC_INTERNAL_H
#define ENC_INTERNAL_H

#include <stdint.h>

#include "enc_real.h"
#include "enc_tables.h"

#define N 160
#define NFFT 512
#define NB 257
#define NSPEC 4
#define NCAND 120
#define NCF 8          /* comb features per candidate: (share, rise) x 4 spectra */
#define TOPK 8
#define LMAX 56
#define VF 73          /* voicing features per frame */
#define HIST 1024      /* past samples kept (windows reach back 472 samples from the frame start) */

typedef struct {
    real log2m[LMAX];
    int L;
    real gamma;
} q_state;

void q_init(q_state *s);
void q_decode(const int *c, int L, const q_state *s, q_state *ns);
void q_quantize(const real *y, int L, const q_state *s, int *c);
void q_pack(const int *c, uint8_t out[7]);

void fft_power(const real *x, int n, real *P);   /* |rfft(x zero-padded to 512)|^2, 257 bins */
real fast_log2(real x);

/* per-frame analysis kept until the frame's voicing is decided */
typedef struct {
    real S[NSPEC][NB];   /* power spectra */
    real lg[NSPEC][NB];  /* log2(S + 1) */
    real C[NCAND][NCF];  /* comb features */
    real logits[NCAND];  /* pitch logits (higher = more likely; non-candidates -1e4) */
    real lv;             /* level (pitch level window) */
    real lvl[4];         /* level features */
    int silent;          /* every analysed sample has the same value (digital silence or DC) */
} frame_an;

void analyse_frame(const real *x, frame_an *a, real *level_acc, int first);
void voicing_frame(const frame_an *a, int b0, real *x, real *raw);
int voicing_classify(const real rows[4][VF]);
void harmonic_amplitudes(const real *P, real w0, int L, const real *v, real *y);
void band_of_harmonics(real f0, int L, int *band);

#endif
