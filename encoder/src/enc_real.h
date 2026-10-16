/* Arithmetic type of the encoder: double by default (bit-exact with the numpy reference); build with
 * -DAMBE_ENC_FLOAT for float (MCUs with a single-precision FPU): then no double arithmetic or double libm call is
 * left anywhere (checked with -Wdouble-promotion), which matters where double is emulated in software (ESP32, ...). */
#ifndef ENC_REAL_H
#define ENC_REAL_H
#include <math.h>
#ifdef AMBE_ENC_FLOAT
typedef float real;
#define R_FLOOR floorf
#define R_FABS fabsf
#define R_EXP expf
#define R_FMOD fmodf
#else
typedef double real;
#define R_FLOOR floor
#define R_FABS fabs
#define R_EXP exp
#define R_FMOD fmod
#endif
#endif
