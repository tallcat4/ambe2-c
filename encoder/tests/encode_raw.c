/* encode_raw in.pcm out.ambe [--background] [--rate N] : raw int16 LE 8 kHz -> frames of AMBE-3000R rate N
 * (default 34: 7-byte frames; no preprocessing) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ambe_enc.h"
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    void *mem = malloc(ambe_enc_size());
    ambe_encoder *e = ambe_enc_init(mem);
    int16_t pcm[160];
    int i, nb = 7;
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--rate") && i + 1 < argc) {
            nb = ambe_enc_set_rate(e, atoi(argv[++i]));
            if (!nb) { fprintf(stderr, "unsupported rate %s\n", argv[i]); return 2; }
        }
    }
    for (i = 3; i < argc; i++)
        if (!strcmp(argv[i], "--background")) ambe_enc_set_background(e, 1);
    uint8_t fr[AMBE_ENC_MAX_BYTES];
    size_t n;
    while ((n = fread(pcm, 2, 160, in)) > 0) {
        for (; n < 160; n++) pcm[n] = 0;
        if (ambe_enc_push(e, pcm, fr)) fwrite(fr, 1, nb, out);
    }
    if (ambe_enc_flush(e, fr)) fwrite(fr, 1, nb, out);
    fclose(out);
    free(mem);
    return 0;
}
