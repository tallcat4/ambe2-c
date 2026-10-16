# Porting rules: blip25-vocoder engine (Rust) -> C99

Source: `third_party/blip25-vocoder/src/engine/` (MIT, Chance Lindsey). Goal: a **bit-exact**, line-by-line C99
translation of the decoder engine. Do not optimise, restructure, "fix" or simplify anything; keep the control flow,
the order of operations and every integer width exactly as in Rust. Keep comments short (one line naming the
original address, e.g. `/* 0x1030b2b0 - advance a 16-bit LCG in place */`); drop the long Rust doc comments.

## Files
The engine is split into four parts, each translated into its own pair of files in `blip25c/src/`:

| part | Rust source | C files |
|---|---|---|
| dsp_a | `dsp.rs` lines 1-2207 | `b25_dsp_a.c`, `b25_dsp_a.h` |
| dsp_b | `dsp.rs` lines 2208-end (incl. `pub mod tables`) | `b25_dsp_b.c`, `b25_dsp_b.h` |
| decode_c | `decode.rs` lines 1-2844 | `b25_decode_c.c`, `b25_decode_c.h` |
| decode_d | `decode.rs` lines 2845-end | `b25_decode_d.c`, `b25_decode_d.h` |

Every `.c` starts with `#include "b25_internal.h"` (helpers, struct typedefs, `b25_tables.h`, **`b25_protos.h`**
and all four part headers). **The prototypes of all functions are fixed in `src/b25_protos.h`** (generated from the
Rust signatures by `tools/gen_protos.py`): implement each function of your part with exactly that signature, and
call other parts' functions through it. If a prototype there is unworkable, report it instead of changing it.
A part header contains only `#define`s for the part's scalar constants and `extern` declarations for its array
constants (other parts may use them). Guard it with `#ifndef B25_<PART>_H`; it must not include anything.

## Names
- Every Rust function `foo` becomes `b25_foo`, everywhere (definitions and calls). Functions are not `static`.
- Constants (`pub const` / `const` / `static`) keep their Rust name with a part prefix:
  `dsp.rs` top level -> `DSP_NAME`; `dsp.rs` inner `pub mod tables { ... }` -> `DSPT_NAME`; `decode.rs` -> `DEC_NAME`.
  `decode.rs` uses the dsp constants unqualified via `use crate::engine::dsp::*;` - map them to their `DSP_` names.
  The dsp top-level constants are: GAIN_CODEBOOK, TWIDDLE, FRAME_COUNTER_WORD, LOG2_C, TONE_SEQ, RECIP_N (check the
  file for any other top-level `const`/`static`; list them in your header). References to
  `crate::engine::tables::X` / `tables::X` that are not in dsp's inner mod become `TB_X` (already generated in
  `b25_tables.h`; see that file for names and types). Inside dsp.rs, `tables::X` refers to the inner `pub mod
  tables` -> `DSPT_X`; its `pub use crate::engine::tables::{COS512, TWIDDLE32 as DFT32_TW}` means
  `tables::COS512` -> `TB_COS512` and `tables::DFT32_TW` -> `TB_TWIDDLE32`.
- Scalar `usize`/`i16` consts -> `#define NAME (value)` in the header (keep the original expression's value).
  Array consts -> defined once in the part's `.c` as `const int16_t NAME[N] = {...};` with `extern` in the header.
- Structs `FrameTables` / `DecTables` -> `b25_frame_tables` / `b25_dec_tables` (defined in `b25_internal.h`);
  pass them as `const b25_frame_tables *` / `const b25_dec_tables *` and access fields with `->`.
  A field that is a slice `&'a [i16]` is a `const int16_t *`.
- If two parts would define the same function name (should not happen), report it rather than renaming silently.

## Types
`i8 i16 i32 i64 u8 u16 u32 u64 usize isize bool` -> `int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t
uint64_t size_t ptrdiff_t int` (bool as `int`, 0/1).
- `&mut [T]` / `&[T]` / `&mut [T; N]` / `&[T; N]` parameters -> `T *` / `const T *` (no length parameter; the
  Rust code indexes within bounds and never calls `.len()` on a parameter). Passing a `&mut` slice where the
  prototype wants `const T *` is fine; passing a `const` table where it wants `T *` needs a cast `(int16_t *)`.
- `&mut T` scalar out-params -> `T *`.
- Sub-slices `&mut x[a..]`, `&x[a..b]` -> `x + a` (a pointer); `split_at_mut(k)` -> two pointers `x` and `x + k`.
- Local arrays `let mut a = [0i16; N];` -> `int16_t a[N] = {0};` (or memset). Tuples returned from a function ->
  out-parameters or a small struct; keep it local to the part if only used there.
- `Option<(i16, i16)>` etc. -> a flag plus values.

## Integer semantics (the important part)
The original is compiled with overflow checks off. Reproduce every operation exactly:
- `a.wrapping_add(b)` etc. -> the helpers in `b25_internal.h` for the operand type:
  i16: `w16_add/sub/mul/neg/shl/shr`, u16: `w16u_*`, i32: `w32_*`, u32: plain C (`+ - *` on `uint32_t` already
  wrap) or `w32u_shl/shr`, i64: `w64_*`, u64: plain C. `wrapping_shl(n)` masks n by the bit width (helpers do).
  `wrapping_abs` -> `w16_abs` / `w32_abs`. `saturating_add/sub` -> `s16_*`/`s32_*`.
- Plain `+ - *` in Rust on **signed** types: the original assumes no overflow; in C write them on values promoted
  to at least `int32_t` and cast back to the Rust type, e.g. i16 `a + b` -> `(int16_t)(a + b)`; i32 `a + b` ->
  `w32_add(a, b)` (use the wrapping helper for every 32/64-bit signed `+ - *` to stay free of UB).
- Plain `<<` on signed values -> the `w*_shl` helper of that type (avoids UB on negative operands). `>>` on signed
  -> `>>` (arithmetic) or `w*_shr`. Shifts on unsigned -> plain C, but mask the count if it can reach the width.
- `x as T` -> `(T)(x)` with the exact C type; chains like `x as u16 as usize` -> `(size_t)(uint16_t)(x)`. Be careful
  with sign: `(x as u32) >> 16` is a logical shift; `(x as i32) >> 16` is arithmetic.
- `!x` on integers -> `~x` cast back to the type. `x.leading_zeros()` -> `lz32`/`lz16`/`lz64` (count for the
  operand's width), `count_ones` -> `popcnt32`, `trailing_zeros` -> `tz32`.
- Integer `/` and `%` -> same in C (truncating); keep operand types.
- `min/max/clamp` -> `b25_imin/imax/iclamp` (or inline comparisons) on values of the right type.
- Remember C integer promotion: `uint16_t * uint16_t` promotes to `int` and can overflow -> use `w16u_mul` or cast
  to `uint32_t` first. Compare signed/unsigned carefully (cast explicitly as Rust's types imply).

## Control flow
- `for i in a..b` -> `for (i = a; i < b; i++)` with the loop variable's Rust type; `(a..b).rev()` -> descending.
- `match` -> `switch` or if/else chains with the same arms; `loop` -> `for (;;)`; labelled breaks -> `goto`.
- `iter()/iter_mut()/zip/enumerate/take/skip` -> explicit index loops; `copy_from_slice` / `copy_within` ->
  `memcpy` / `memmove`; `fill(v)` -> a loop (or memset for 0); `swap(i, j)` -> temp swap.
- `debug_assert!` -> drop. `assert!`/`unreachable!`/`panic!` -> drop if purely a check, keep the control flow.
- Closures -> static helper functions or inline code.
- `#[cfg(test)]` modules and items -> skip entirely.

## Output
Compile your part with `cc -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
-Wno-unused-but-set-variable -Wno-unused-function -c blip25c/src/b25_<part>.c -I blip25c/src -o /dev/null` and
fix every error and every warning that points at a real issue. Other parts' headers start as empty stubs; if you
need another part's constant before its owner has written it, look up its value in the Rust source and use the
prefixed name anyway (e.g. `DSP_TWIDDLE`), then compile once the stub is filled - a missing name at that point
is fine, report it in your summary.
