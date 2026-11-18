#include <cstdio>
#include <cstdint>
#include <cstring>
#include "../firmware/plaits/upstream/plaits/wave_inflate_probe.h"
/* One stream on stdin -> exit 2 if the decoder refuses it, else the decoded bytes on stdout.
 * --batch: many streams in one process (tests/run_wave_inflate_probe.py). Each record on stdin is
 * a 4-byte little-endian length and the stream; each reply on stdout is one byte (1 decoded,
 * 0 refused), a 4-byte little-endian count and the decoded bytes. Every stream is decoded into
 * buffers of the same sizes as the single-stream mode, freshly cleared. */
static int one(const uint8_t *src, size_t n, FILE *out)
{
    uint8_t in[12000], dec[7756];
    memset(in, 0, sizeof in); memset(dec, 0, sizeof dec);
    if (n > sizeof in) n = sizeof in;
    memcpy(in, src, n);
    uint32_t m = 0;
    uint8_t ok = wave_inflate_probe(in, n, dec, sizeof dec, &m) ? 1u : 0u;
    if (!ok) m = 0;
    uint8_t hdr[5] = {ok, (uint8_t)m, (uint8_t)(m >> 8), (uint8_t)(m >> 16), (uint8_t)(m >> 24)};
    return fwrite(hdr, 1, 5, out) != 5 || fwrite(dec, 1, m, out) != m;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--batch")) {
        static uint8_t buf[12000];
        uint8_t len[4];
        while (fread(len, 1, 4, stdin) == 4) {
            uint32_t n = len[0] | len[1] << 8 | len[2] << 16 | (uint32_t)len[3] << 24;
            if (n > sizeof buf || fread(buf, 1, n, stdin) != n) return 3;
            if (one(buf, n, stdout)) return 1;
        }
        return fflush(stdout) != 0;
    }
    uint8_t in[12000], out[7756]; size_t n = fread(in, 1, sizeof in, stdin); uint32_t m = 0;
    if (!wave_inflate_probe(in, n, out, sizeof out, &m)) return 2;
    return fwrite(out, 1, m, stdout) != m;
}
