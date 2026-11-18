/* Host-only migration helper. Validate a v5 envelope with the current project
 * decoder before compressing it for the existing project-import protocol. */
#include "pattern_project.h"
#include "pattern_storage_codec.h"
#include "instrument_session_v17.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t wire[FM1_PROJECT_MAX], session[FM1_PROJECT_SESSION];
static uint8_t scratch[FM1_PROJECT_SESSION], encoded[FM1_PROJECT_RECORD_MAX];
static uint8_t arena[60000], work_arena[60000], compressed[3584];
static br_pattern_snapshot pattern, live;
static br_pattern_bank bank;
static fm1_pattern_song song;
static br_session_v17_t decoded;
static int valid(const uint8_t *p, size_t n, void *context)
{
    uint32_t clamps = 0;
    (void)context;
    return br_v17_decode(p, (uint32_t)n, &decoded, &clamps) == BR_V17_OK && !clamps;
}
int main(int argc, char **argv)
{
    size_t n, compressed_n = 0;
    FILE *f;
    fm1_project_work work = {0};
    if (argc != 3 || !(f = fopen(argv[1], "rb"))) return 1;
    n = fread(wire, 1, sizeof wire, f);
    if (fgetc(f) != EOF || ferror(f) || fclose(f)) return 1;
    work.pattern = &pattern; work.session = scratch;
    work.arena = work_arena; work.arena_capacity = sizeof work_arena;
    work.compressed = encoded; work.compressed_capacity = sizeof encoded;
    if (fm1_project_decode(wire, n, valid, 0, &work, session, &bank,
                           arena, sizeof arena, &live, &song) != FM1_PROJECT_OK) {
        fputs("Current project decoder rejected converted project.\n", stderr); return 1;
    }
    if (fm1_pattern_codec_encode(wire, n, compressed, sizeof compressed, &compressed_n) != FM1_CODEC_OK) {
        fputs("Converted project does not fit a saved slot.\n", stderr); return 1;
    }
    f = fopen(argv[2], "wb");
    if (!f) return 1;
    if (fwrite(compressed, 1, compressed_n, f) != compressed_n || fclose(f)) return 1;
    printf("Validated %u patterns, active %u; compressed project %zu bytes.\n",
           (unsigned)__builtin_popcount(song.used), (unsigned)song.active + 1u, compressed_n);
    return 0;
}
