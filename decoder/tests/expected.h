/* FNV-1a 64 of blip25-vocoder 1.0 (Rust) outputs for the files in tests/vectors (mbe...: mbelib-compatible API, PCM clipped
 * to +-32760 as mbelib does). Generated in the neural-ambe2 research repository. */
static const struct { const char *test, *vector; unsigned long long fnv; } EXPECTED[] = {
    {"dec2450", "speech", 0xf5350e1ecbf2a13fULL},
    {"dec2450", "status", 0xd946c20fa1867c88ULL},
    {"dec2450", "random", 0x22a4d79e410817d8ULL},
    {"fec3600", "chip3600", 0x4f34dfc20f4fac22ULL},
    {"dec3600", "chip3600", 0xf3f84a300132d0e4ULL},
    {"mbe3600_pcm", "chip3600", 0xf3f84a300132d0e4ULL},
    {"mbe3600_ambe_d", "chip3600", 0x895f13cb2efbbe95ULL},
    {"fec3600", "noisy3600", 0x5fb64b96a498db73ULL},
    {"dec3600", "noisy3600", 0xc1cde0ba2bd12206ULL},
    {"mbe3600_pcm", "noisy3600", 0xc1cde0ba2bd12206ULL},
    {"mbe3600_ambe_d", "noisy3600", 0x06fd37b68aee4d4dULL},
    {"mbe2450_pcm", "speech", 0x1f35c89f49c965cfULL},
    {"fecsoft", "soft_noisy", 0xdf17a46beee940f7ULL},
    {"decsoft", "soft_noisy", 0xc102e2fbb6db57b8ULL},
    {"fecsoft", "soft_ties", 0x528ff920c8570a35ULL},
    {"decsoft", "soft_ties", 0xce082a491ab80141ULL},
    {"fecsoft", "soft_harsh", 0xbcc122d268ebe89eULL},
    {"decsoft", "soft_harsh", 0xc1cc043f9610b15bULL},
    {"fecsoft", "soft_random", 0xd9b8e25549dc7590ULL},
    {"decsoft", "soft_random", 0x2ed8e0f42cbe4855ULL},
};
