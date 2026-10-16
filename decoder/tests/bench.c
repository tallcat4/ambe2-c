/* Decode speed: bench frames.bin (7-byte ambe_d frames), decoded 10 times. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "blip25.h"
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    static unsigned char buf[7 * 30000];
    size_t n = fread(buf, 7, 30000, f), k;
    blip25_decoder d;
    int16_t pcm[160];
    long sum = 0;
    int r;
    clock_t t0 = clock();
    for (r = 0; r < 10; r++) {
        blip25_init(&d);
        for (k = 0; k < n; k++) { blip25_decode_2450(&d, buf + 7 * k, pcm); sum += pcm[17]; }
    }
    double us = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / (10.0 * n);
    printf("%zu frames x10: %.2f us/frame (%.0fx real time)  [%ld]\n", n, us, 20000.0 / us, sum);
    return 0;
}
