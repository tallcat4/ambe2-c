/* Internal header of the C port of blip25-vocoder's AMBE+2 half-rate decoder engine.
 *
 * Integer semantics: the Rust original is built with overflow checks off and spells every operation that may
 * overflow as wrapping_*; the helpers below reproduce those operations exactly without C undefined behaviour.
 * Requirements on the C implementation (true for GCC, Clang and MSVC on every common target): two's-complement
 * integers, conversion to a narrower signed type keeps the low bits, and >> on a negative value is arithmetic.
 */
#ifndef B25_INTERNAL_H
#define B25_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "b25_tables.h"

#if defined(_MSC_VER)
#define B25_INLINE static __inline
#else
#define B25_INLINE static inline
#endif

/* ---- i16 */
B25_INLINE int16_t w16_add(int16_t a, int16_t b) { return (int16_t)(uint16_t)((uint32_t)(uint16_t)a + (uint16_t)b); }
B25_INLINE int16_t w16_sub(int16_t a, int16_t b) { return (int16_t)(uint16_t)((uint32_t)(uint16_t)a - (uint16_t)b); }
B25_INLINE int16_t w16_mul(int16_t a, int16_t b) { return (int16_t)(uint16_t)((uint32_t)(uint16_t)a * (uint16_t)b); }
B25_INLINE int16_t w16_neg(int16_t a) { return (int16_t)(uint16_t)(0u - (uint16_t)a); }
B25_INLINE int16_t w16_shl(int16_t a, uint32_t n) { return (int16_t)(uint16_t)((uint32_t)(uint16_t)a << (n & 15)); }
B25_INLINE int16_t w16_shr(int16_t a, uint32_t n) { return (int16_t)(a >> (n & 15)); }          /* arithmetic */
B25_INLINE int16_t w16_abs(int16_t a) { return a < 0 ? w16_neg(a) : a; }
B25_INLINE int16_t s16_add(int16_t a, int16_t b) { int32_t s = (int32_t)a + b; return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); }
B25_INLINE int16_t s16_sub(int16_t a, int16_t b) { int32_t s = (int32_t)a - b; return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); }

/* ---- u16 */
B25_INLINE uint16_t w16u_add(uint16_t a, uint16_t b) { return (uint16_t)((uint32_t)a + b); }
B25_INLINE uint16_t w16u_sub(uint16_t a, uint16_t b) { return (uint16_t)((uint32_t)a - b); }
B25_INLINE uint16_t w16u_mul(uint16_t a, uint16_t b) { return (uint16_t)((uint32_t)a * b); }
B25_INLINE uint16_t w16u_shl(uint16_t a, uint32_t n) { return (uint16_t)((uint32_t)a << (n & 15)); }
B25_INLINE uint16_t w16u_shr(uint16_t a, uint32_t n) { return (uint16_t)(a >> (n & 15)); }

/* ---- i32 */
B25_INLINE int32_t w32_add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
B25_INLINE int32_t w32_sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
B25_INLINE int32_t w32_mul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
B25_INLINE int32_t w32_neg(int32_t a) { return (int32_t)(0u - (uint32_t)a); }
B25_INLINE int32_t w32_shl(int32_t a, uint32_t n) { return (int32_t)((uint32_t)a << (n & 31)); }
B25_INLINE int32_t w32_shr(int32_t a, uint32_t n) { return a >> (n & 31); }                     /* arithmetic */
B25_INLINE int32_t w32_abs(int32_t a) { return a < 0 ? w32_neg(a) : a; }
B25_INLINE int32_t s32_add(int32_t a, int32_t b) { int64_t s = (int64_t)a + b; return (int32_t)(s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s); }
B25_INLINE int32_t s32_sub(int32_t a, int32_t b) { int64_t s = (int64_t)a - b; return (int32_t)(s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s); }

/* ---- u32 */
B25_INLINE uint32_t w32u_shl(uint32_t a, uint32_t n) { return a << (n & 31); }
B25_INLINE uint32_t w32u_shr(uint32_t a, uint32_t n) { return a >> (n & 31); }
B25_INLINE uint32_t rotl32(uint32_t a, uint32_t n) { n &= 31; return n ? (a << n) | (a >> (32 - n)) : a; }
B25_INLINE uint32_t rotr32(uint32_t a, uint32_t n) { n &= 31; return n ? (a >> n) | (a << (32 - n)) : a; }

/* ---- i64 / u64 */
B25_INLINE int64_t w64_add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
B25_INLINE int64_t w64_sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
B25_INLINE int64_t w64_mul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
B25_INLINE int64_t w64_neg(int64_t a) { return (int64_t)(0u - (uint64_t)a); }
B25_INLINE int64_t w64_shl(int64_t a, uint32_t n) { return (int64_t)((uint64_t)a << (n & 63)); }
B25_INLINE int64_t w64_shr(int64_t a, uint32_t n) { return a >> (n & 63); }                     /* arithmetic */

/* ---- bit counting (Rust leading_zeros / trailing_zeros / count_ones; defined for 0) */
B25_INLINE uint32_t lz32(uint32_t x) { uint32_t n = 0; if (!x) return 32; while (!(x & 0x80000000u)) { x <<= 1; n++; } return n; }
B25_INLINE uint32_t lz16(uint16_t x) { return lz32((uint32_t)x) - 16; }
B25_INLINE uint32_t lz64(uint64_t x) { return (x >> 32) ? lz32((uint32_t)(x >> 32)) : 32 + lz32((uint32_t)x); }
B25_INLINE uint32_t tz32(uint32_t x) { uint32_t n = 0; if (!x) return 32; while (!(x & 1u)) { x >>= 1; n++; } return n; }
B25_INLINE uint32_t popcnt32(uint32_t x) { uint32_t n = 0; while (x) { x &= x - 1; n++; } return n; }

/* ---- min / max / clamp on plain integers (Rust Ord::min/max/clamp) */
B25_INLINE int32_t b25_imin(int32_t a, int32_t b) { return a < b ? a : b; }
B25_INLINE int32_t b25_imax(int32_t a, int32_t b) { return a > b ? a : b; }
B25_INLINE int32_t b25_iclamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- constant-table bundles (Rust FrameTables / DecTables) */
typedef struct {
    const int16_t *submode;            /* 7 x AMBE sub-mode selector */
    const uint32_t *voicing_patterns;  /* per-band voicing dibit patterns */
    const int16_t *gain32;             /* 32 x Q11 frame-gain codebook */
    const int16_t *block_lens;         /* per-block vector lengths */
    const int16_t *prba9;              /* 9-bit PRBA codebook */
    const int16_t *prba7;              /* 7-bit PRBA codebook */
    const int16_t *coef_books;         /* higher-order-coefficient codebooks */
    const int16_t *nibbles;            /* packed per-harmonic quantiser bit allocation */
    const int16_t *gain64;             /* 64 x Q11 block-gain codebook */
    const int16_t *hoc_weight;         /* higher-order-coefficient weights */
    const int16_t *hoc_loading;        /* higher-order-coefficient loading factors */
    const uint16_t *codewords;         /* voicing / tone codewords */
    const int16_t *blob;               /* cosine table and the tone/config blob above it */
} b25_frame_tables;

typedef struct {
    b25_frame_tables frame;
    const int16_t *hilbert;            /* 10 Hilbert / differentiating taps */
    const int16_t *sqrt_tbl;           /* 81 x round(32767 sqrt(i/80)) */
} b25_dec_tables;

#include "b25_protos.h"
#include "b25_dsp_a.h"
#include "b25_dsp_b.h"
#include "b25_decode_c.h"
#include "b25_decode_d.h"

#endif
