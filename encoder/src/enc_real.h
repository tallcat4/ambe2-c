/* Arithmetic type of the encoder: double by default (matches the numpy reference closely); build with
 * -DAMBE_ENC_FLOAT for float (MCUs with a single-precision FPU). */
#ifndef ENC_REAL_H
#define ENC_REAL_H
#ifdef AMBE_ENC_FLOAT
typedef float real;
#else
typedef double real;
#endif
#endif
