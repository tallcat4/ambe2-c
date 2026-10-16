/* host: dec_hashes rate34.ambe rate33.bin > dec_expect.h  (the hashes dec_test.c checks against) */
#include <stdio.h>
#include <stdlib.h>

#include "mcu_test.h"

uint64_t ambe_dec_run(const uint8_t *frames, long n, int rate33, uint32_t (*read_ticks)(void), uint64_t *total,
                      uint32_t *cmax);

static uint8_t *slurp(const char *p, long *n) {
    FILE *f = fopen(p, "rb");
    uint8_t *b;
    if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END), *n = ftell(f), fseek(f, 0, SEEK_SET);
    b = malloc(*n);
    if (fread(b, 1, *n, f) != (size_t)*n) exit(1);
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    long n34, n33;
    uint64_t t;
    uint32_t m;
    uint8_t *a = slurp(argv[1], &n34), *b = slurp(argv[2], &n33);
    (void)argc;
    printf("#define DEC_EXPECT_34 0x%016llxULL\n", (unsigned long long)ambe_dec_run(a, n34 / 7, 0, NULL, &t, &m));
    printf("#define DEC_EXPECT_33 0x%016llxULL\n", (unsigned long long)ambe_dec_run(b, n33 / 9, 1, NULL, &t, &m));
    return 0;
}
