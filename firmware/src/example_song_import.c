/* SPDX-License-Identifier: GPL-3.0-only */
#include "example_song_import.h"
#include <string.h>

typedef struct {
    uint8_t part, step, value, velocity;
    uint16_t length;
} fm1_example_event;
typedef struct {
    uint8_t slot, length, division;
    uint16_t offset, count;
    const char *name;
} fm1_example_pattern;
typedef struct {
    const char *title, *description;
    uint32_t tempo, swing, preset[BR_PARTS];
    const fm1_example_pattern *patterns; uint32_t pattern_count;
    const fm1_example_event *events; uint32_t event_count;
    const fm1_song_entry *entries; uint32_t entry_count;
} fm1_example_definition;
#include "../gen/example_song_data.inc"

const char *fm1_example_pattern_name(unsigned id, unsigned slot)
{
    if (id>=FM1_EXAMPLE_SONG_COUNT) return 0;
    const fm1_example_definition *d=&fm1_examples[id];
    for (uint32_t i=0u;i<d->pattern_count;++i)
        if (d->patterns[i].slot==slot) return d->patterns[i].name;
    return 0;
}

static int overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    return an && bn && (x<=y ? y-x<an : x-y<bn);
}
static int distinct(const void *const *ptr, const size_t *size, unsigned count)
{
    for (unsigned i=0u;i<count;++i) {
        if (!ptr[i] || !size[i]) return 0;
        for (unsigned j=0u;j<i;++j) if (overlap(ptr[i],size[i],ptr[j],size[j])) return 0;
    }
    return 1;
}
static void snapshot_defaults(br_pattern_snapshot *s, uint32_t length, uint32_t division)
{
    memset(s,0,sizeof *s);
    for (uint32_t part=0u;part<BR_PARTS;++part) {
        s->length[part]=length; s->division[part]=division;
    }
    memset(s->synth_note,255,sizeof s->synth_note);
    memset(s->synth_velocity,255,sizeof s->synth_velocity);
    memset(s->kit_note,255,sizeof s->kit_note);
    memset(s->kit_pitch,255,sizeof s->kit_pitch);
    memset(s->kit_velocity,100,sizeof s->kit_velocity);
}
static int place(br_pattern_snapshot *s, const fm1_example_event *e)
{
    if (e->part>=BR_PARTS || e->step>=s->length[e->part] || !e->velocity ||
        e->velocity>127u || !e->length || e->length>BR_STEP_LENGTH_MAX) return BR_BANK_INVALID;
    if (e->part==BR_DRUM_PART) {
        if (e->value>=BR_DRUM_LANES || (s->step[e->part][e->step]&(1u<<e->value))) return BR_BANK_INVALID;
        s->step[e->part][e->step] |= 1u<<e->value;
        s->kit_velocity[e->step][e->value]=e->velocity;
        s->kit_length[e->step][e->value]=e->length;
    } else {
        if (e->value>127u || (s->step[e->part][e->step]&BR_STEP_ON)) return BR_BANK_INVALID;
        s->step[e->part][e->step]=BR_NOTE_STEP(e->value,e->velocity);
        s->synth_note[e->part][e->step]=e->value;
        s->synth_velocity[e->part][e->step]=e->velocity;
        s->synth_length[e->part][e->step]=e->length;
    }
    return BR_BANK_OK;
}
int fm1_example_song_import(unsigned id, br_pattern_bank *bank,
    uint8_t *arena, size_t arena_capacity, br_pattern_snapshot *live,
    br_pattern_snapshot *scratch, uint8_t *encoded, size_t encoded_capacity,
    fm1_pattern_song *song, fm1_example_settings *settings)
{
    const void *ptr[]={bank,arena,live,scratch,encoded,song,settings};
    const size_t size[]={sizeof *bank,arena_capacity,sizeof *live,sizeof *scratch,
                         encoded_capacity,sizeof *song,sizeof *settings};
    const fm1_example_definition *d; fm1_pattern_song staged_song;
    fm1_example_settings staged_settings; int result;
    if (id>=FM1_EXAMPLE_SONG_COUNT || !distinct(ptr,size,7u)) return BR_BANK_INVALID;
    d=&fm1_examples[id];
    if (!d->pattern_count || d->pattern_count>FM1_PATTERN_COUNT || !d->entry_count ||
        d->entry_count>FM1_SONG_ENTRIES || d->patterns[0].slot!=0u) return BR_BANK_INVALID;
    result=br_pattern_bank_init(bank,arena,arena_capacity);
    if (result!=BR_BANK_OK) return result;
    fm1_pattern_song_init(&staged_song);
    for (uint32_t i=0u;i<d->pattern_count;++i) {
        const fm1_example_pattern *p=&d->patterns[i];
        if (p->slot>=FM1_PATTERN_COUNT || !p->length || p->length>BR_STEPS || p->division>2u ||
            p->offset>d->event_count || p->count>d->event_count-p->offset) return BR_BANK_INVALID;
        snapshot_defaults(scratch,p->length,p->division);
        for (uint32_t j=0u;j<p->count;++j) {
            result=place(scratch,&d->events[p->offset+j]);
            if (result!=BR_BANK_OK) return result;
        }
        result=br_pattern_bank_paste(bank,p->slot,live,scratch,encoded,encoded_capacity);
        if (result!=BR_BANK_OK) return result;
    }
    staged_song.used=bank->used;
    for (uint32_t i=0u;i<d->entry_count;++i)
        if (!fm1_song_entry_set(&staged_song,(uint8_t)i,d->entries[i].pattern,d->entries[i].bars))
            return BR_BANK_INVALID;
    if (!fm1_song_length_set(&staged_song,(uint8_t)d->entry_count)) return BR_BANK_INVALID;
    staged_settings.title=d->title; staged_settings.description=d->description;
    staged_settings.tempo=d->tempo; staged_settings.swing=d->swing;
    for (uint32_t p=0u;p<BR_PARTS;++p) staged_settings.preset[p]=d->preset[p];
    *song=staged_song; *settings=staged_settings;
    return BR_BANK_OK;
}
