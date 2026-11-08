/* SPDX-License-Identifier: GPL-3.0-only */
#if !defined(BR_PATTERN_BANK_EXPERIMENT) || !BR_PATTERN_BANK_EXPERIMENT
#error "Pattern owner is an opt-in host-only experiment"
#endif
#include "pattern_owner.h"
#include <string.h>

#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
#define BR_PATTERN_POOL __attribute__((section(".pool")))
#else
#define BR_PATTERN_POOL
#endif
fm1_pattern_song br_ps_state BR_PATTERN_POOL;
static br_pattern_bank bank BR_PATTERN_POOL;
static br_pattern_snapshot *editable, *staging, *clipboard;
static uint8_t *scratch;
static size_t scratch_size;
static int (*quiescent)(void *);
static void *quiescent_context;
static int initialized, clipboard_valid, workspace_lent;
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
static br_pattern_irq_publication *runtime_pub;
static unsigned runtime_state, runtime_target, runtime_slot, runtime_fault;
#define RUNTIME_BUSY() (runtime_state != 0u)
#else
#define RUNTIME_BUSY() 0
#endif
static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    if(!an || !bn) return 0;
    return x<=y ? y-x<an : x-y<bn;
}
static int separate(const void *a, size_t an, const void *b, size_t bn,
                    const void *c, size_t cn, const void *d, size_t dn,
                    const void *e, size_t en) {
    const void *p[]={a,b,c,d,e}; size_t n[]={an,bn,cn,dn,en}; unsigned i,j;
    for(i=0;i<5;i++) for(j=0;j<i;j++) if(overlaps(p[i],n[i],p[j],n[j])) return 0;
    return 1;
}
static int ready(void) { return initialized && quiescent && quiescent(quiescent_context); }
int br_ps_host_init(uint8_t *arena, size_t capacity, br_pattern_snapshot *live,
    br_pattern_snapshot *work, br_pattern_snapshot *clip, uint8_t *encoded,
    size_t encoded_capacity, int (*is_quiescent)(void *), void *context)
{
    if(RUNTIME_BUSY() || workspace_lent) return 0;
    initialized=0;
    if(!arena || !live || !work || !encoded || !encoded_capacity ||
       !is_quiescent || !separate(arena,capacity,live,sizeof *live,work,sizeof *work,
           clip,clip?sizeof *clip:0u,encoded,encoded_capacity) || !is_quiescent(context) ||
       br_pattern_bank_init(&bank,arena,capacity)!=BR_BANK_OK) return 0;
    editable=live; staging=work; clipboard=clip; scratch=encoded; scratch_size=encoded_capacity;
    quiescent=is_quiescent; quiescent_context=context; clipboard_valid=0;
    if(br_pattern_snapshot_capture(editable)) return 0;
    fm1_pattern_song_init(&br_ps_state); initialized=1; return 1;
}
int br_ps_host_workspace_acquire(br_pattern_snapshot **previous_live,
    br_pattern_snapshot **stage, uint8_t **out_scratch, size_t *capacity)
{
    if(!ready() || RUNTIME_BUSY() || bank.reserved || workspace_lent ||
       !previous_live || !stage || !out_scratch || !capacity) return 0;
    workspace_lent=1; *previous_live=editable; *stage=staging;
    *out_scratch=scratch; *capacity=scratch_size; return 1;
}
void br_ps_host_workspace_release(void) { workspace_lent=0; }
int br_ps_host_can_reset(void)
{
    if(!ready() || RUNTIME_BUSY() || workspace_lent || bank.reserved || !editable ||
       !bank.arena || !bank.capacity || overlaps(&bank,sizeof bank,bank.arena,bank.capacity)) return 0;
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
    if(runtime_pub && (br_pattern_irq_edits_blocked() || br_pattern_irq_error())) return 0;
#endif
    return 1;
}
int br_ps_host_reset(void)
{
    uint8_t *arena; size_t capacity;
    if(!br_ps_host_can_reset() || br_pattern_snapshot_capture(editable)) return 0;
    arena=bank.arena; capacity=bank.capacity;
    if(br_pattern_bank_init(&bank,arena,capacity)!=BR_BANK_OK) return 0;
    fm1_pattern_song_init(&br_ps_state); clipboard_valid=0;
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
    runtime_fault=0;
#endif
    return 1;
}
int br_ps_host_can_adopt(const br_pattern_bank *source, const br_pattern_snapshot *active,
                         const fm1_pattern_song *song)
{
    unsigned i;
    if(!ready() || RUNTIME_BUSY() || workspace_lent || !source || !active || !song || !source->arena ||
       source->active>=FM1_PATTERN_COUNT || source->reserved ||
       !(source->used&(1u<<source->active)) || (source->used&~((1u<<FM1_PATTERN_COUNT)-1u)) ||
       source->used!=song->used ||
       source->active!=song->active || song->queued!=FM1_PATTERN_NONE ||
       song->song_enabled ||
       !separate(source->arena,source->capacity,editable,sizeof *editable,
           staging,sizeof *staging,clipboard,sizeof *clipboard,scratch,scratch_size) ||
       overlaps(active,sizeof *active,staging,sizeof *staging) ||
       overlaps(active,sizeof *active,editable,sizeof *editable) ||
       overlaps(active,sizeof *active,clipboard,sizeof *clipboard) ||
       overlaps(active,sizeof *active,scratch,scratch_size) ||
       overlaps(active,sizeof *active,source->arena,source->capacity) ||
       overlaps(song,sizeof *song,staging,sizeof *staging) ||
       overlaps(song,sizeof *song,editable,sizeof *editable) ||
       overlaps(song,sizeof *song,clipboard,sizeof *clipboard) ||
       overlaps(song,sizeof *song,scratch,scratch_size) ||
       overlaps(song,sizeof *song,source->arena,source->capacity) ||
       overlaps(source,sizeof *source,staging,sizeof *staging) ||
       overlaps(source,sizeof *source,editable,sizeof *editable) ||
       overlaps(source,sizeof *source,clipboard,sizeof *clipboard) ||
       overlaps(source,sizeof *source,scratch,scratch_size) ||
       overlaps(source,sizeof *source,source->arena,source->capacity)) return 0;
    for(i=0;i<FM1_PATTERN_COUNT;i++) if(source->used&(1u<<i))
        if(br_pattern_bank_copy(source,i,active,staging)!=BR_BANK_OK) return 0;
    return 1;
}
int br_ps_host_adopt(const br_pattern_bank *source, const br_pattern_snapshot *active,
                     const fm1_pattern_song *song)
{
    if(!br_ps_host_can_adopt(source,active,song)) return 0;
    memcpy(editable,active,sizeof *editable);
    if(br_pattern_snapshot_apply(editable)) return 0;
    bank=*source; br_ps_state=*song; clipboard_valid=0;
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
    runtime_fault=0;
#endif
    return 1;
}
int br_ps_host_can_adopt_copy(const br_pattern_bank *source, const br_pattern_snapshot *active,
                              const fm1_pattern_song *song)
{
    if(!ready() || RUNTIME_BUSY() || !source || !active || !song || !source->arena || source->reserved ||
       source->active>=FM1_PATTERN_COUNT || source->used!=song->used || source->active!=song->active ||
       song->queued!=FM1_PATTERN_NONE || song->song_enabled ||
       source->size>bank.capacity || overlaps(source->arena,source->capacity,bank.arena,bank.capacity) ||
       !separate(source->arena,source->capacity,editable,sizeof *editable,
           staging,sizeof *staging,clipboard,clipboard?sizeof *clipboard:0u,scratch,scratch_size) ||
       overlaps(active,sizeof *active,editable,sizeof *editable) ||
       (active!=staging && overlaps(active,sizeof *active,staging,sizeof *staging)) ||
       overlaps(active,sizeof *active,clipboard,clipboard?sizeof *clipboard:0u) ||
       overlaps(active,sizeof *active,scratch,scratch_size) ||
       overlaps(active,sizeof *active,source->arena,source->capacity) ||
       overlaps(song,sizeof *song,editable,sizeof *editable) || overlaps(song,sizeof *song,staging,sizeof *staging) ||
       overlaps(song,sizeof *song,clipboard,clipboard?sizeof *clipboard:0u) || overlaps(song,sizeof *song,scratch,scratch_size) ||
       overlaps(song,sizeof *song,source->arena,source->capacity) ||
       overlaps(source,sizeof *source,editable,sizeof *editable) || overlaps(source,sizeof *source,staging,sizeof *staging) ||
       overlaps(source,sizeof *source,clipboard,clipboard?sizeof *clipboard:0u) || overlaps(source,sizeof *source,scratch,scratch_size) ||
       overlaps(source,sizeof *source,source->arena,source->capacity)) return 0;
    return br_pattern_bank_verify(source)==BR_BANK_OK;
}
int br_ps_host_adopt_copy(const br_pattern_bank *source, const br_pattern_snapshot *active,
                          const fm1_pattern_song *song)
{
    uint8_t *arena; size_t capacity;
    if(!br_ps_host_can_adopt_copy(source,active,song)) return 0;
    /* After preflight there are no fallible operations: audio and editors
     * are quiescent and source storage remains stable until return. */
    arena=bank.arena; capacity=bank.capacity;
    memcpy(arena,source->arena,source->size);
    bank=*source; bank.arena=arena; bank.capacity=capacity;
    memcpy(editable,active,sizeof *editable);
    (void)br_pattern_snapshot_apply(editable);
    br_ps_state=*song; clipboard_valid=0;
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
    runtime_fault=0;
#endif
    return 1;
}
int br_ps_host_export(br_pattern_bank *view, br_pattern_snapshot *active, fm1_pattern_song *song)
{
    if(!ready() || RUNTIME_BUSY() || bank.reserved || !view || !active || !song ||
       overlaps(view,sizeof *view,active,sizeof *active) || overlaps(view,sizeof *view,song,sizeof *song) ||
       overlaps(active,sizeof *active,song,sizeof *song) ||
       overlaps(view,sizeof *view,bank.arena,bank.capacity) || overlaps(active,sizeof *active,bank.arena,bank.capacity) ||
       overlaps(song,sizeof *song,bank.arena,bank.capacity) ||
       overlaps(view,sizeof *view,editable,sizeof *editable) || overlaps(view,sizeof *view,staging,sizeof *staging) ||
       overlaps(view,sizeof *view,clipboard,sizeof *clipboard) ||
       (active!=editable && overlaps(active,sizeof *active,editable,sizeof *editable)) ||
       overlaps(active,sizeof *active,staging,sizeof *staging) || overlaps(active,sizeof *active,clipboard,sizeof *clipboard) ||
       overlaps(song,sizeof *song,editable,sizeof *editable) || overlaps(song,sizeof *song,staging,sizeof *staging) ||
       overlaps(song,sizeof *song,clipboard,sizeof *clipboard) ||
       overlaps(view,sizeof *view,scratch,scratch_size) || overlaps(active,sizeof *active,scratch,scratch_size) ||
       overlaps(song,sizeof *song,scratch,scratch_size)) return 0;
    if(br_pattern_snapshot_capture(active)) return 0;
    *view=bank; *song=br_ps_state; song->queued=FM1_PATTERN_NONE;
    song->song_enabled=0; song->song_position=0; song->bars_elapsed=0; return 1;
}
int br_ps_select(uint32_t target)
{
    if(!ready() || RUNTIME_BUSY() || workspace_lent || target>=FM1_PATTERN_COUNT || br_pattern_snapshot_capture(editable)) return 0;
    if(br_pattern_bank_select(&bank,target,editable,staging,scratch,scratch_size)!=BR_BANK_OK) return 0;
    if(br_pattern_snapshot_apply(editable)) return 0;
    br_ps_state.active=bank.active; br_ps_state.used=bank.used;
    br_ps_state.queued=FM1_PATTERN_NONE; br_ps_state.song_enabled=0; return 1;
}
int br_ps_copy_pattern(uint32_t source)
{
    if(!ready() || RUNTIME_BUSY() || workspace_lent || source>=FM1_PATTERN_COUNT || br_pattern_snapshot_capture(editable)) return 0;
    if(clipboard) {
        if(br_pattern_bank_copy(&bank,source,editable,clipboard)!=BR_BANK_OK) return 0;
    } else if(br_pattern_bank_clip_copy(&bank,source,editable,staging,scratch,scratch_size)!=BR_BANK_OK) return 0;
    clipboard_valid=1; return 1;
}
int br_ps_paste_pattern(uint32_t target)
{
    if(!ready() || RUNTIME_BUSY() || workspace_lent || !clipboard_valid || target>=FM1_PATTERN_COUNT || br_pattern_snapshot_capture(editable)) return 0;
    if(clipboard) {
        if(br_pattern_bank_paste(&bank,target,editable,clipboard,scratch,scratch_size)!=BR_BANK_OK) return 0;
    } else if(br_pattern_bank_clip_paste(&bank,target,editable,staging,scratch,scratch_size)!=BR_BANK_OK) return 0;
    if(target==bank.active && br_pattern_snapshot_apply(editable)) return 0;
    br_ps_state.used=bank.used; return 1;
}
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
/* Host experiment only. Audio reads the immutable slot while foreground
 * copies globals; this requires an independently audited reader/writer gate. */
int br_ps_runtime_bind(br_pattern_irq_publication *pub, br_pattern_snapshot *a,
                       br_pattern_snapshot *b, br_pattern_snapshot *c)
{
    (void)b; (void)c;
    /* The single immutable mailbox slot may alias the owner's staging buffer:
     * all foreground mutators are blocked while a request is outstanding. */
    if(!ready() || workspace_lent || !pub || !a || runtime_state || bank.reserved ||
       overlaps(pub,sizeof *pub,a,sizeof *a) || overlaps(pub,sizeof *pub,bank.arena,bank.capacity) ||
       overlaps(pub,sizeof *pub,scratch,scratch_size) || overlaps(pub,sizeof *pub,editable,sizeof *editable) ||
       overlaps(pub,sizeof *pub,staging,sizeof *staging) || overlaps(pub,sizeof *pub,clipboard,sizeof *clipboard) ||
       overlaps(a,sizeof *a,bank.arena,bank.capacity) || overlaps(a,sizeof *a,scratch,scratch_size) ||
       overlaps(a,sizeof *a,editable,sizeof *editable) || overlaps(a,sizeof *a,clipboard,sizeof *clipboard) ||
       (a!=staging && overlaps(a,sizeof *a,staging,sizeof *staging)) ||
       !br_pattern_irq_init(pub,a,0,0) || br_pattern_snapshot_capture(a) || !br_pattern_irq_audio_bind(pub)) return 0;
    runtime_pub=pub; runtime_fault=0; return 1;
}
int br_ps_runtime_queue(uint32_t target)
{
    unsigned slot; int r;
    /* The caller must hold the audio foreground claim: it protects a
     * boundary from racing snapshot capture and mailbox publication. */
    if(!initialized || !runtime_pub || runtime_state || runtime_fault || workspace_lent || br_pattern_irq_error() ||
       br_pattern_irq_edits_blocked() || target>=FM1_PATTERN_COUNT || target==bank.active) return 0;
    slot=br_pattern_irq_claim(runtime_pub); if(slot==BR_PATTERN_IRQ_NO_SLOT) return 0;
    if(br_pattern_snapshot_capture(editable)) { br_pattern_irq_abandon(runtime_pub,slot); return 0; }
    r=br_pattern_bank_prepare(&bank,target,editable,runtime_pub->slot[slot],scratch_size);
    if(r!=BR_BANK_OK) { br_pattern_irq_abandon(runtime_pub,slot); return 0; }
    if(!br_pattern_irq_submit(runtime_pub,slot)) {
        br_pattern_bank_cancel(&bank); br_pattern_irq_abandon(runtime_pub,slot); return 0;
    }
    runtime_slot=slot; runtime_target=target; runtime_state=1;
    br_ps_state.queued=(uint8_t)target; return 1;
}
int br_ps_runtime_cancel_stopped(void)
{
    if(!ready() || workspace_lent || !runtime_pub || runtime_state!=1u || br_pattern_irq_edits_blocked() ||
       !br_pattern_irq_cancel_unseen(runtime_pub)) return 0;
    if(br_pattern_bank_cancel(&bank)!=BR_BANK_OK) return 0;
    runtime_state=0; runtime_fault=0; br_ps_state.song_enabled=0;
    br_ps_state.queued=FM1_PATTERN_NONE; return 1;
}
int br_ps_runtime_reset_stopped(void)
{
    if(!ready() || workspace_lent || !runtime_pub || br_pattern_irq_edits_blocked()) return 0;
    if(runtime_state && !br_ps_runtime_cancel_stopped()) return 0;
    if(br_pattern_irq_error() && !br_pattern_irq_audio_reset(runtime_pub)) return 0;
    runtime_fault=0;  br_ps_state.song_enabled=0;
    br_ps_state.queued=FM1_PATTERN_NONE; return 1;
}
static int runtime_failure(void)
{
    runtime_fault=1; br_ps_state.song_enabled=0;
    /* Stop requests are single-word foreground writes; the audio engine's
     * ordinary transport transition stops sounding sequencer voices. */
    br_playing=0; fm1_memory_publish_sync(); return -1;
}
int br_ps_runtime_service(void)
{
    if(!runtime_pub || !runtime_state) return 0;
    if(br_pattern_irq_error()) {
        (void)runtime_failure();
        /* A consumed request must still return audio from the staged slot;
         * audio processes globals_ready even after reporting its fault. */
        if(runtime_state!=2 && runtime_state!=3 && !br_pattern_irq_edits_blocked()) return -1;
    }
    if(runtime_state==1 && br_pattern_irq_edits_blocked()) {
        /* Gate ensures no competing pattern writer; audio reads staged slot. */
        if(br_pattern_snapshot_capture(editable) ||
           br_pattern_bank_commit(&bank,editable,runtime_pub->slot[runtime_slot],scratch,scratch_size)!=BR_BANK_OK) {
            /* Commit failures leave globals and bank unchanged. Return audio
             * to those original globals before canceling the reservation. */
            (void)runtime_failure();
            if(!br_pattern_irq_globals_ready()) return -1;
            runtime_state=3;
        } else {
            if(br_pattern_snapshot_apply(editable) || !br_pattern_irq_globals_ready()) return runtime_failure();
            runtime_state=2;
        }
    }
    if(runtime_state==3) {
        br_pattern_irq_poll(runtime_pub);
        if(!runtime_pub->outstanding && !br_pattern_irq_edits_blocked()) {
            if(bank.reserved && br_pattern_bank_cancel(&bank)!=BR_BANK_OK) return -1;
            br_ps_state.queued=FM1_PATTERN_NONE; runtime_state=0; return -1;
        }
        return 0;
    }
    if(runtime_state==2) {
        br_pattern_irq_poll(runtime_pub);
        if(!runtime_pub->outstanding && !br_pattern_irq_edits_blocked()) {
            br_ps_state.active=(uint8_t)runtime_target; br_ps_state.used=bank.used;
            br_ps_state.queued=FM1_PATTERN_NONE;
            br_ps_state.song_enabled=0;
            runtime_state=0; return runtime_fault ? -1 : 1;
        }
    }
    return 0;
}
#endif
