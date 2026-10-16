/* Fixed-point (integer-only) encoder: shared types and arithmetic helpers.
 *
 * Number formats (Qn = value * 2^n):
 *   spectra      log2(P + 1) per bin, Q8 int16 (P = |FFT|^2 of the windowed input in input units; the linear power
 *                is recovered from it with fx_pow when needed, to 0.3%)
 *   comb         energy share Q15, log rise Q8 (int16)
 *   features     Q16 int32 (log2 units, ratios); MLP inputs standardised to Q8 int16
 *   pitch costs  Q16; quantiser (log2 amplitudes, gains, codebooks) Q16; cosines Q14; fractions Q12
 * No floating point, no division in the per-bin loops; 64-bit products where noted. */
#ifndef FX_INTERNAL_H
#define FX_INTERNAL_H

#include <stdint.h>

#include "fx_tables.h"

#define N 160
#define NFFT 512
#define NB 257
#define NSPEC 4
#define NCAND 120
#define NCF 8
#define TOPK 8
#define LMAX 56
#define VF 73
#define HIST 1024
#define G56 120          /* pitch-grid index of the L = 56 grid (b1 = 18..31) */

typedef struct {
    int32_t log2m[LMAX];   /* Q16 */
    int L;
    int32_t gamma;         /* Q16 */
} fxq_state;

typedef struct {
    int16_t lg[NSPEC][NB];   /* log2(P + 1), Q8 */
    int16_t C[NCAND][NCF];   /* per spectrum k: [2k] share Q15, [2k + 1] rise Q8 */
    int32_t logits[NCAND];   /* Q16 (non-candidates FX_NOT_CAND) */
    int32_t lv;              /* pitch level, Q16 */
    int32_t lvl[4];          /* level features, Q16 */
    int silent;
} fx_an;

#define FX_NOT_CAND ((int32_t)-655360000)   /* -1e4 in Q16 */

/* optional stage profiling (tests/mcu): -DFX_PROFILE, the harness defines fx_prof_mark */
enum { FXP_FFT, FXP_LOG, FXP_COMB, FXP_LEVEL, FXP_PREFILTER, FXP_PITCH_MLP, FXP_VITERBI, FXP_VOICING_FEAT,
       FXP_CLASSIFY, FXP_BACKGROUND, FXP_AMPLITUDES, FXP_QUANT, FXP_N };
#ifdef FX_PROFILE
void fx_prof_mark(int stage);   /* charges the time since the previous mark to `stage` */
#define FX_PROF(s) fx_prof_mark(s)
#else
#define FX_PROF(s) ((void)0)
#endif

/* products */
static inline int32_t fx_mul16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

/* two 16 x 16 products added to acc (ARM DSP SMLAD; inline assembly: older toolchains lack the ACLE intrinsic) */
#if defined(__ARM_FEATURE_DSP)
static inline int32_t fx_smlad(int32_t x, int32_t y, int32_t acc) {
    int32_t r;
    __asm__("smlad %0, %1, %2, %3" : "=r"(r) : "r"(x), "r"(y), "r"(acc));
    return r;
}
#endif

/* floor(a * c / 2^16) for a 16-bit c, exactly, on every CPU: the high word of one 32 x 32 product where the CPU has
 * it, two 32-bit products on cores without (ARMv6-M: Cortex-M0 / M0+ / M1; or -DFX_NO_LONG_MUL) */
#if defined(__ARM_ARCH_6M__) || defined(FX_NO_LONG_MUL)
static inline int32_t fx_mulq16(int32_t a, int32_t c) { return (a >> 16) * c + (((int32_t)(a & 0xffff) * c) >> 16); }
#else   /* the high word of a 32 x 32 product (SMULL, MULH, MULSH): floor(a * (c << 16) / 2^32) = floor(a * c / 2^16) */
static inline int32_t fx_mulq16(int32_t a, int32_t c) { return (int32_t)(((int64_t)a * (int32_t)((uint32_t)c << 16)) >> 32); }
#endif
static inline int32_t fx_mul14(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 14); }

/* dsp */
void fx_fft_lg(const int16_t *x, const int16_t *w, int n, int16_t *lg);   /* windowed 512-point log power, Q8 */
int32_t fx_log2_u64(uint64_t v);                    /* log2(v), v >= 1, Q16 */
int32_t fx_log2p1(uint64_t v, int e);               /* log2(v * 2^e + 1), Q16 */
uint64_t fx_pow(int32_t lg_q8);                     /* 2^(lg / 256) - 1, Q4 */

/* analysis */
void fx_analyse_frame(const int16_t *x, fx_an *a, int32_t *level_acc, int first);
void fx_voicing_frame(const fx_an *a, int b0, int32_t *x, int32_t *raw);
int fx_voicing_classify(const int32_t rows[4][VF]);
void fx_harmonic_amplitudes(const int16_t *lg0, int g, int L, const uint8_t *v, int32_t *y);
void fx_standardise(const int32_t *x, const int32_t *mu, const int32_t *invsd, int n, int16_t *z);
void fx_layer(const int16_t *W, int nin, const int32_t *mult, const int32_t *b, const int16_t *in, int nout, int relu,
              int32_t *out);   /* nin: multiple of 8 (rows and input zero-padded) */
void fx_hidden(const int32_t *h, int n, int16_t *z);   /* layer output -> next layer's input (clipped to [0, 64)) */

/* quantiser */
void fxq_init(fxq_state *s);
void fxq_decode(const int *c, int L, const fxq_state *s, fxq_state *ns);
void fxq_quantize(const int32_t *y, int L, const fxq_state *s, int *c);
void fxq_pack(const int *c, uint8_t out[7]);

#endif
