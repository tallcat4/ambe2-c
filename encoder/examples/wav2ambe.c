/* wav2ambe in.wav out.ambe [--no-normalize] [--background]
 * 8 kHz mono 16-bit PCM WAV -> 7-byte AMBE-3000R rate-34 frames. By default the input is scaled to an active speech
 * level of -26 dBFS (the level the encoder was trained at); resample other formats first, e.g.
 *   sox in.wav -r 8000 -c 1 -b 16 in8k.wav      or      ffmpeg -i in.wav -ar 8000 -ac 1 in8k.wav
 * --background: send silence / background noise as b0 = 124 background-noise frames (as ECMODE 0x0800). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ambe_enc.h"

static int has_opt(int argc, char **argv, const char *opt) {
    int i;
    for (i = 3; i < argc; i++)
        if (!strcmp(argv[i], opt)) return 1;
    return 0;
}

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

int main(int argc, char **argv) {
    FILE *f, *o;
    unsigned char *b;
    long n, i, ns = 0, k;
    int16_t *pcm = NULL;
    uint8_t fr[7];
    void *mem;
    ambe_encoder *e;
    if (argc < 3) { fprintf(stderr, "usage: wav2ambe in.wav out.ambe [--no-normalize] [--background]\n"); return 2; }
    if (!(f = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(n);
    if (fread(b, 1, n, f) != (size_t)n || n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { fprintf(stderr, "not a WAV file\n"); return 1; }
    fclose(f);
    for (i = 12; i + 8 <= n;) {   /* chunks */
        unsigned long len = rd32(b + i + 4);
        if (!memcmp(b + i, "fmt ", 4) && (rd16(b + i + 8) != 1 || rd16(b + i + 10) != 1 || rd32(b + i + 12) != 8000 || rd16(b + i + 22) != 16)) {
            fprintf(stderr, "need 8 kHz mono 16-bit PCM (resample with sox or ffmpeg first)\n");
            return 1;
        }
        if (!memcmp(b + i, "data", 4)) {
            ns = (long)(len / 2);
            if (i + 8 + ns * 2 > n) ns = (n - i - 8) / 2;
            pcm = malloc((ns + 160 * 4 + 160) * sizeof(int16_t));
            for (k = 0; k < ns; k++) pcm[k] = (int16_t)rd16(b + i + 8 + 2 * k);
        }
        i += 8 + len + (len & 1);
    }
    if (!pcm) { fprintf(stderr, "no data chunk\n"); return 1; }
    if (!has_opt(argc, argv, "--no-normalize")) {   /* active level: 20 ms frames within 40 dB of the loudest */
        double top = -1e9, sum = 0, g;
        long cnt = 0;
        for (k = 0; k + 160 <= ns; k += 160) {
            double ee = 1e-12;
            for (i = 0; i < 160; i++) ee += (pcm[k + i] / 32768.0) * (pcm[k + i] / 32768.0) / 160;
            if (10 * log10(ee) > top) top = 10 * log10(ee);
        }
        for (k = 0; k + 160 <= ns; k += 160) {
            double ee = 1e-12;
            for (i = 0; i < 160; i++) ee += (pcm[k + i] / 32768.0) * (pcm[k + i] / 32768.0) / 160;
            if (10 * log10(ee) > top - 40) sum += ee, cnt++;
        }
        if (cnt && sum > 1e-10) {
            g = pow(10.0, (-26.0 - 10 * log10(sum / cnt)) / 20);
            for (k = 0; k < ns; k++) {
                double v = pcm[k] * g;
                pcm[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : floor(v + 0.5));
            }
        }
    }
    k = (ns + 159) / 160 * 160 + 160 * 4;   /* whole frames + 80 ms of silence to flush the delay */
    memset(pcm + ns, 0, (k - ns) * sizeof(int16_t));
    mem = malloc(ambe_enc_size());
    e = ambe_enc_init(mem);
    if (has_opt(argc, argv, "--background")) ambe_enc_set_background(e, 1);
    if (!(o = fopen(argv[2], "wb"))) { perror(argv[2]); return 1; }
    for (i = 0; i < k; i += 160)
        if (ambe_enc_push(e, pcm + i, fr)) fwrite(fr, 1, 7, o);
    if (ambe_enc_flush(e, fr)) fwrite(fr, 1, 7, o);
    fclose(o);
    fprintf(stderr, "%ld frames -> %s\n", k / 160, argv[2]);
    return 0;
}
