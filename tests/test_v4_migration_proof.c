/* Host proof: validate a converted historical project with the current decoder. */
#include "pattern_project.h"
#include "instrument_session_v17.h"
#include <assert.h>
#include <stdio.h>
static uint8_t wire[FM1_PROJECT_MAX], session[FM1_PROJECT_SESSION];
static uint8_t scratch[FM1_PROJECT_SESSION], encoded[FM1_PROJECT_RECORD_MAX];
static uint8_t arena[60000], work_arena[60000];
static br_pattern_snapshot pattern, live;
static br_pattern_bank bank;
static fm1_pattern_song song;
static br_session_v17_t decoded;
static int valid(const uint8_t *p, size_t n, void *context)
{
    (void)context;
    return br_v17_decode(p, (uint32_t)n, &decoded, 0) == BR_V17_OK;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    size_t n = fread(wire, 1, sizeof wire, f);
    assert(fgetc(f) == EOF && !ferror(f)); fclose(f);
    fm1_project_work work = {0};
    work.pattern = &pattern; work.session = scratch;
    work.arena = work_arena; work.arena_capacity = sizeof work_arena;
    work.compressed = encoded; work.compressed_capacity = sizeof encoded;
    assert(fm1_project_decode(wire, n, valid, 0, &work, session, &bank,
                             arena, sizeof arena, &live, &song) == FM1_PROJECT_OK);
    assert(song.used == 0x3fu && song.active == 0u && decoded.global.bpm == 120);
    puts("converted v4 project: current decoder accepts all six historical patterns");
    return 0;
}
