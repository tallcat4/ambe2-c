/* encoder speed: bench in.pcm [--background] (raw int16 8 kHz), encoded 5 times */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ambe_enc.h"
int main(int argc, char **argv) {
    FILE *f;
    long n, k, r;
    int16_t *pcm;
    uint8_t fr[7];
    unsigned sum = 0;
    void *mem = malloc(ambe_enc_size());
    clock_t t0;
    if (argc < 2) return 2;
    f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END); n = ftell(f) / 320; fseek(f, 0, SEEK_SET);
    pcm = malloc(n * 320);
    if (fread(pcm, 320, n, f) != (size_t)n) return 1;
    t0 = clock();
    for (r = 0; r < 5; r++) {
        ambe_encoder *e = ambe_enc_init(mem);
        if (argc > 2 && !strcmp(argv[2], "--background")) ambe_enc_set_background(e, 1);
        for (k = 0; k < n; k++)
            if (ambe_enc_push(e, pcm + 160 * k, fr)) sum += fr[3];
        if (ambe_enc_flush(e, fr)) sum += fr[3];
    }
    {
        double us = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / (5.0 * n);
        printf("%s%ld frames x5: %.1f us/frame (%.0fx real time), state %lu bytes [%u]\n", argc > 2 ? "background mode: " : "", n, us, 20000.0 / us, ambe_enc_size(), sum);
    }
    return 0;
}
