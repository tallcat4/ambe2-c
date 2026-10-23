/* ambe2wav in.ambe out.wav [--ambe-d | --rate N]
 * AMBE-3000R channel frames -> 8 kHz mono 16-bit WAV.
 *   default   7-byte frames in rate-34 (ThumbDV) order (what ambe-enc's wav2ambe writes)
 *   --ambe-d  7-byte frames in ambe_d (mbelib / DSD .amb) order
 *   --rate N  frames of AMBE-3000R RATET N, MSB first: 34 (7 bytes), 33 (9, what wav2ambe --fec writes), and the
 *             AMBE+ rates 31 (5), 0 (6), 22 (10), 17 (10), 26 (16). --rate33 is the same as --rate 33.
 *   --rate 26 --chand  AMBE-3000R channel-packet fields instead of fixed frames: field id (0x01 CHAND, packed bits,
 *             or 0x17 CHAND4, 4-bit soft decisions), bit count, data. Any bit count is decoded as the chip does
 *             (blip25_decode_ambeplus_r26_chand). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blip25.h"

static void put32(unsigned char *p, unsigned long v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static int frame_bytes(int rate) {
    switch (rate) {
    case 34: return 7;
    case 33: return 9;
    case 31: return 5;
    case 0: return 6;
    case 22: case 17: return 10;
    case 26: return 16;
    default: return 0;
    }
}

int main(int argc, char **argv) {
    FILE *f, *o;
    unsigned char fr[128], h[44];
    int16_t pcm[160];
    long frames = 0, errors = 0;
    int ambe_d = argc > 3 && !strcmp(argv[3], "--ambe-d");
    int rate = argc > 3 && !strcmp(argv[3], "--rate33") ? 33 : argc > 4 && !strcmp(argv[3], "--rate") ? atoi(argv[4]) : 34;
    int nb = frame_bytes(rate);
    int chand = rate == 26 && argc > 5 && !strcmp(argv[5], "--chand");
    blip25_decoder dec;
    if (argc < 3 || !nb) {
        fprintf(stderr, "usage: ambe2wav in.ambe out.wav [--ambe-d | --rate 34|33|31|0|22|17|26 [--chand]]\n");
        return 2;
    }
    if (!(f = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
    if (!(o = fopen(argv[2], "wb"))) { perror(argv[2]); return 1; }
    fwrite(h, 1, 44, o);   /* header written at the end */
    blip25_init(&dec);
    for (;;) {
        int i, e = 0;
        if (chand) {
            unsigned char hd[2];
            size_t len;
            if (fread(hd, 1, 2, f) != 2) break;
            len = hd[0] == 0x17 ? (hd[1] + 1u) / 2 : (hd[1] + 7u) / 8;
            if ((hd[0] != 0x01 && hd[0] != 0x17) || fread(fr, 1, len, f) != len) break;
            if (hd[0] == 0x17) blip25_decode_ambeplus_r26_chand4(&dec, fr, hd[1], pcm, &e);
            else blip25_decode_ambeplus_r26_chand(&dec, fr, hd[1], pcm, &e);
        } else if (fread(fr, 1, (size_t)nb, f) != (size_t)nb) break;
        else if (ambe_d) blip25_decode_2450(&dec, fr, pcm);
        else if (rate == 34) blip25_decode_2450_thumbdv(&dec, fr, pcm);
        else if (rate == 33) blip25_decode_3600(&dec, fr, pcm, &e);
        else if (rate == 31) blip25_decode_ambeplus_r31(&dec, fr, pcm);
        else if (rate == 0) blip25_decode_ambeplus_r0(&dec, fr, pcm);
        else if (rate == 22) blip25_decode_ambeplus_r22(&dec, fr, pcm, &e);
        else if (rate == 17) blip25_decode_ambeplus_r17(&dec, fr, pcm);
        else blip25_decode_ambeplus_r26(&dec, fr, pcm, &e);
        errors += e;
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
    if (rate == 33 || rate == 22 || rate == 26) fprintf(stderr, "%ld frames -> %s (%ld bits corrected)\n", frames, argv[2], errors);
    else fprintf(stderr, "%ld frames -> %s\n", frames, argv[2]);
    return 0;
}
