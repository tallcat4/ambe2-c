/* Standalone decoder tests: decode every vector in tests/vectors through the native and the mbelib-compatible API
 * and compare FNV-1a 64 hashes of the outputs with the references in tests/expected.h (blip25-vocoder for the
 * AMBE+2 rates; for the AMBE+ rates the chip's own speech bits / the Python FEC for the FEC tests, and earlier
 * outputs of this decoder for the decode tests).
 *   run_tests [vector dir]      (default: tests/vectors) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blip25.h"
#include "mbelib.h"
#include "expected.h"

typedef struct { unsigned long long h; } fnv;
static void fnv_init(fnv *f) { f->h = 0xcbf29ce484222325ULL; }
static void fnv_bytes(fnv *f, const unsigned char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) { f->h ^= b[i]; f->h *= 0x100000001b3ULL; }
}
static void fnv_pcm(fnv *f, const int16_t *p, int n) {   /* little-endian int16 */
    int i;
    for (i = 0; i < n; i++) {
        unsigned char b[2] = {(unsigned char)(p[i] & 0xff), (unsigned char)((p[i] >> 8) & 0xff)};
        fnv_bytes(f, b, 2);
    }
}

static const char *DIR = "tests/vectors";
static unsigned char *slurp(const char *name, const char *ext, long *n) {
    char path[512];
    FILE *fp;
    unsigned char *b;
    snprintf(path, sizeof(path), "%s/%s%s", DIR, name, ext);
    if (!(fp = fopen(path, "rb"))) { perror(path); exit(1); }
    fseek(fp, 0, SEEK_END); *n = ftell(fp); fseek(fp, 0, SEEK_SET);
    b = malloc(*n ? *n : 1);
    if (fread(b, 1, *n, fp) != (size_t)*n) { perror(path); exit(1); }
    fclose(fp);
    return b;
}

static void unpack7(const unsigned char *fr, uint8_t bits[49]) {
    int i;
    for (i = 0; i < 49; i++) bits[i] = (fr[i >> 3] >> (7 - (i & 7))) & 1;
}

static void pack_fec(fnv *f, const uint8_t bits[49], const uint8_t e[4]) {
    unsigned char p[7] = {0};
    int i;
    for (i = 0; i < 49; i++) p[i >> 3] |= (unsigned char)(bits[i] << (7 - (i & 7)));
    fnv_bytes(f, p, 7);
    fnv_bytes(f, e, 4);
}

/* DSD / pyambelib dibit deinterleave of a rate-33 frame into mbelib's ambe_fr[4][24] */
static const int rW[36] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2};
static const int rX[36] = {23, 10, 22, 9, 21, 8, 20, 7, 19, 6, 18, 5, 17, 4, 16, 3, 15, 2, 14, 1, 13, 0, 12, 10, 11, 9, 10, 8, 9, 7, 8, 6, 7, 5, 6, 4};
static const int rY[36] = {0, 2, 0, 2, 0, 2, 0, 2, 0, 3, 0, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3};
static const int rZ[36] = {5, 3, 4, 2, 3, 1, 2, 0, 1, 13, 0, 12, 22, 11, 21, 10, 20, 9, 19, 8, 18, 7, 17, 6, 16, 5, 15, 4, 14, 3, 13, 2, 12, 1, 11, 0};

static unsigned long long run(const char *test, const char *v) {
    long n, ns, k;
    unsigned char *fr;
    blip25_decoder dec;
    int16_t pcm[160];
    fnv f;
    fnv_init(&f);
    blip25_init(&dec);
    if (!strcmp(test, "dec2450")) {
        unsigned char *st;
        fr = slurp(v, ".bin", &n);
        st = slurp(v, ".st", &ns);
        for (k = 0; k < n / 7; k++) {
            uint8_t bits[49];
            unpack7(fr + 7 * k, bits);
            blip25_decode_bits(&dec, bits, (st[k] & 0x80) ? BLIP25_LOST : (st[k] & 15), pcm);
            fnv_pcm(&f, pcm, 160);
        }
        free(st);
    } else if (!strcmp(test, "fec3600") || !strcmp(test, "dec3600")) {
        fr = slurp(v, ".bin", &n);
        for (k = 0; k < n / 9; k++) {
            if (test[0] == 'f') {
                uint8_t bits[49], e[4];
                blip25_fec_3600(fr + 9 * k, bits, e);
                pack_fec(&f, bits, e);
            } else {
                blip25_decode_3600(&dec, fr + 9 * k, pcm, NULL);
                fnv_pcm(&f, pcm, 160);
            }
        }
    } else if (!strcmp(test, "fecsoft") || !strcmp(test, "decsoft")) {
        fr = slurp(v, ".llr", &n);
        for (k = 0; k < n / 72; k++) {
            if (test[0] == 'f') {
                uint8_t bits[49], e[4];
                blip25_fec_3600_soft((const int8_t *)fr + 72 * k, bits, e);
                pack_fec(&f, bits, e);
            } else {
                blip25_decode_3600_soft(&dec, (const int8_t *)fr + 72 * k, pcm);
                fnv_pcm(&f, pcm, 160);
            }
        }
    } else if (!strncmp(test, "apfec", 5)) {   /* AMBE+ FEC: packed speech bits */
        int r22 = !strcmp(test, "apfec22");
        int nb = r22 ? 10 : 16, K = r22 ? 48 : 80;
        fr = slurp(v, ".bin", &n);
        for (k = 0; k < n / nb; k++) {
            uint8_t sp[80];
            unsigned char p[10] = {0};
            int i;
            if (r22) blip25_fec_ambeplus_r22(fr + nb * k, sp);
            else blip25_fec_ambeplus_r26(fr + nb * k, sp);
            for (i = 0; i < K; i++) p[i >> 3] |= (unsigned char)(sp[i] << (7 - (i & 7)));
            fnv_bytes(&f, p, (size_t)(K / 8));
        }
    } else if (!strcmp(test, "apchand")) {   /* RATET 26 packets with explicit bit counts: field, nbits, data */
        size_t at = 0;
        fr = slurp(v, ".bin", &n);
        while (at + 2 <= (size_t)n) {
            int nbits = fr[at + 1], soft = fr[at] == 0x17;
            const unsigned char *x = fr + at + 2;
            if (soft) blip25_decode_ambeplus_r26_chand4(&dec, x, nbits, pcm, NULL);
            else blip25_decode_ambeplus_r26_chand(&dec, x, nbits, pcm, NULL);
            fnv_pcm(&f, pcm, 160);
            at += 2 + (size_t)(soft ? (nbits + 1) / 2 : (nbits + 7) / 8);
        }
    } else if (!strncmp(test, "apdec", 5)) {   /* AMBE+ decode: apdec<RATET> */
        int rate = atoi(test + 5);
        int nb = rate == 31 ? 5 : rate == 0 ? 6 : rate == 26 ? 16 : 10;
        fr = slurp(v, ".bin", &n);
        for (k = 0; k < n / nb; k++) {
            const unsigned char *x = fr + nb * k;
            if (rate == 31) blip25_decode_ambeplus_r31(&dec, x, pcm);
            else if (rate == 0) blip25_decode_ambeplus_r0(&dec, x, pcm);
            else if (rate == 22) blip25_decode_ambeplus_r22(&dec, x, pcm, NULL);
            else if (rate == 17) blip25_decode_ambeplus_r17(&dec, x, pcm);
            else blip25_decode_ambeplus_r26(&dec, x, pcm, NULL);
            fnv_pcm(&f, pcm, 160);
        }
    } else {   /* mbelib-compatible API */
        mbe_parms cur, prev, prev_enh;
        short out[160];
        char ambe_d[49], err_str[64];
        int errs, errs2, i;
        mbe_initMbeParms(&cur, &prev, &prev_enh);
        if (!strcmp(test, "mbe2450_pcm")) {
            fr = slurp(v, ".bin", &n);
            for (k = 0; k < n / 7; k++) {
                for (i = 0; i < 49; i++) ambe_d[i] = (char)((fr[7 * k + i / 8] >> (7 - i % 8)) & 1);
                errs = errs2 = 0;
                mbe_processAmbe2450Data(out, &errs, &errs2, err_str, ambe_d, &cur, &prev, &prev_enh, 3);
                fnv_pcm(&f, out, 160);
            }
        } else {
            fr = slurp(v, ".bin", &n);
            for (k = 0; k < n / 9; k++) {
                char ambe_fr[4][24];
                memset(ambe_fr, 0, sizeof(ambe_fr));
                for (i = 0; i < 36; i++) {
                    ambe_fr[rW[i]][rX[i]] = (char)((fr[9 * k + (2 * i) / 8] >> (7 - (2 * i) % 8)) & 1);
                    ambe_fr[rY[i]][rZ[i]] = (char)((fr[9 * k + (2 * i + 1) / 8] >> (7 - (2 * i + 1) % 8)) & 1);
                }
                mbe_processAmbe3600x2450Frame(out, &errs, &errs2, err_str, ambe_fr, ambe_d, &cur, &prev, &prev_enh, 3);
                if (!strcmp(test, "mbe3600_pcm")) fnv_pcm(&f, out, 160);
                else fnv_bytes(&f, (const unsigned char *)ambe_d, 49);
            }
        }
    }
    free(fr);
    return f.h;
}

int main(int argc, char **argv) {
    size_t i, fails = 0, n = sizeof(EXPECTED) / sizeof(EXPECTED[0]);
    if (argc > 1) DIR = argv[1];
    for (i = 0; i < n; i++) {
        unsigned long long h = run(EXPECTED[i].test, EXPECTED[i].vector);
        int ok = h == EXPECTED[i].fnv;
        printf("%-15s %-12s %s\n", EXPECTED[i].test, EXPECTED[i].vector, ok ? "identical to reference" : "DIFFERENT");
        fails += !ok;
    }
    printf("%zu/%zu passed\n", n - fails, n);
    return fails ? 1 : 0;
}
