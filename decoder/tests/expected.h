/* FNV-1a 64 of blip25-vocoder 1.0 (Rust) outputs for the files in tests/vectors (mbe...: mbelib-compatible API, PCM clipped
 * to +-32760 as mbelib does). Generated in the neural-ambe2 research repository. */
static const struct { const char *test, *vector; unsigned long long fnv; } EXPECTED[] = {
    {"dec2450", "speech", 0x2463f7466503f29fULL},
    {"dec2450", "status", 0xdcb4fd0337f47306ULL},
    {"dec2450", "random", 0xd7ec0cb15349c650ULL},
    {"fec3600", "chip3600", 0x8fd46f15b0db95aaULL},
    {"dec3600", "chip3600", 0x12c7bbf8276a02fcULL},
    {"mbe3600_pcm", "chip3600", 0x12c7bbf8276a02fcULL},
    {"mbe3600_ambe_d", "chip3600", 0x2817bebaaa9db8f6ULL},
    {"fec3600", "noisy3600", 0x3995784b0a69391dULL},
    {"dec3600", "noisy3600", 0xebe48a386391255eULL},
    {"mbe3600_pcm", "noisy3600", 0xebe48a386391255eULL},
    {"mbe3600_ambe_d", "noisy3600", 0x09ee64621322f3e5ULL},
    {"mbe2450_pcm", "speech", 0xf054ece73bae80a3ULL},
    {"fecsoft", "soft_noisy", 0x248940f3b7e192b1ULL},
    {"decsoft", "soft_noisy", 0x32459df6d77a584cULL},
    {"fecsoft", "soft_ties", 0xa4326b55186898bbULL},
    {"decsoft", "soft_ties", 0x8f8fbde3d5b194cfULL},
    {"fecsoft", "soft_harsh", 0x07e1c83c500f31b4ULL},
    {"decsoft", "soft_harsh", 0xa15bcf69fd30c867ULL},
    {"fecsoft", "soft_random", 0x5c027e4c1448a48cULL},
    {"decsoft", "soft_random", 0xc7c22244736d0946ULL},
};
