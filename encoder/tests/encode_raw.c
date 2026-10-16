/* encode_raw in.pcm out.ambe : raw int16 LE 8 kHz -> 7-byte rate-34 frames (no preprocessing) */
#include <stdio.h>
#include <stdlib.h>
#include "ambe_enc.h"
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    void *mem = malloc(ambe_enc_size());
    ambe_encoder *e = ambe_enc_init(mem);
    int16_t pcm[160];
    uint8_t fr[7];
    size_t n;
    while ((n = fread(pcm, 2, 160, in)) > 0) {
        for (; n < 160; n++) pcm[n] = 0;
        if (ambe_enc_push(e, pcm, fr)) fwrite(fr, 1, 7, out);
    }
    if (ambe_enc_flush(e, fr)) fwrite(fr, 1, 7, out);
    fclose(out);
    free(mem);
    return 0;
}
