/* ambe2wav in.ambe out.wav [--ambe-d]
 * 7-byte frames -> 8 kHz mono 16-bit WAV. Frames are in AMBE-3000R rate-34 (ThumbDV) order by default (what
 * ambe-enc's wav2ambe writes); --ambe-d reads ambe_d (mbelib / DSD .amb) order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blip25.h"

static void put32(unsigned char *p, unsigned long v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

int main(int argc, char **argv) {
    FILE *f, *o;
    unsigned char fr[7], h[44];
    int16_t pcm[160];
    long frames = 0;
    int ambe_d = argc > 3 && !strcmp(argv[3], "--ambe-d");
    blip25_decoder dec;
    if (argc < 3) { fprintf(stderr, "usage: ambe2wav in.ambe out.wav [--ambe-d]\n"); return 2; }
    if (!(f = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
    if (!(o = fopen(argv[2], "wb"))) { perror(argv[2]); return 1; }
    fwrite(h, 1, 44, o);   /* header written at the end */
    blip25_init(&dec);
    while (fread(fr, 1, 7, f) == 7) {
        int i;
        if (ambe_d) blip25_decode_2450(&dec, fr, pcm);
        else blip25_decode_2450_thumbdv(&dec, fr, pcm);
        for (i = 0; i < 160; i++) { unsigned char s[2] = {(unsigned char)(pcm[i] & 0xff), (unsigned char)((pcm[i] >> 8) & 0xff)}; fwrite(s, 1, 2, o); }
        frames++;
    }
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + frames * 320); memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0; put32(h + 24, 8000); put32(h + 28, 16000); h[32] = 2; h[33] = 0;
    h[34] = 16; h[35] = 0; memcpy(h + 36, "data", 4); put32(h + 40, frames * 320);
    fseek(o, 0, SEEK_SET);
    fwrite(h, 1, 44, o);
    fclose(o);
    fclose(f);
    fprintf(stderr, "%ld frames -> %s\n", frames, argv[2]);
    return 0;
}
