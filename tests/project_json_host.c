/* Host-side prototype: turn validated, text-fed pattern data into PSC3.
 * Never connects to an instrument or writes device storage. */
#include "pattern_project_fixture.h"
#include "instrument_bank.h"
#include <stdlib.h>

static void die(const char *s) { fprintf(stderr,"project: %s\n",s); exit(1); }
static unsigned get(void) { unsigned x;if(scanf("%u",&x)!=1)die("invalid input");return x; }
static void defaults(br_pattern_snapshot *p) {
    memset(p,0,sizeof *p);
    for(unsigned i=0;i<BR_PARTS;i++) {p->length[i]=32;p->division[i]=1;}
    memset(p->synth_note,255,sizeof p->synth_note);
    memset(p->synth_velocity,255,sizeof p->synth_velocity);
    memset(p->kit_note,255,sizeof p->kit_note);
    memset(p->kit_pitch,255,sizeof p->kit_pitch);
    memset(p->kit_velocity,100,sizeof p->kit_velocity);
}
int main(int argc,char **argv) {
    size_t n=0; unsigned bpm,swing,count,entries,loop; br_bank_state_t bs; FILE *f;
    if(argc!=2)die("output path required");
    bpm=get();swing=get();count=get();entries=get();loop=get();
    if(bpm<60||bpm>240||swing>77||count!=11||entries>64||loop>1)die("unsupported song metadata");
    br_app_init();if(!br_pattern_session_bind(&session_scratch,sizeof session_scratch))die("session scratch");
    for(unsigned i=0;i<3;i++){unsigned index=get();if(index>=BR_BANK_COUNT)die("sound index");br_bank_state(&br_bank[index],&bs);br_bank_put(&bs,&br_part[i].sound,&br_part[i].mod);}
    br_kit_bank_apply(&br_kit_bank[9],&br_part[3].sound);br_bpm=bpm;br_swing=swing;
    if(br_pattern_bank_init(&source,source_arena,7756)!=BR_BANK_OK)die("bank init");
    fm1_pattern_song_init(&src_song);
    for(unsigned p=0;p<count;p++) {
        unsigned slot=get(), length=get(),division=get(),events=get();
        if(slot!=p||length!=32||division!=1)die("invalid pattern");
        defaults(&src_work);
        for(unsigned j=0;j<events;j++) {
            unsigned part=get(),step=get(),value=get(),velocity=get(),duration=get();
            if(part>=4||step>=32||velocity<1||velocity>127||duration<1||duration>BR_STEP_LENGTH_MAX)die("invalid event");
            if(part==3) {if(value>=4||(src_work.step[part][step]&(1u<<value)))die("duplicate drum");src_work.step[part][step]|=1u<<value;src_work.kit_velocity[step][value]=velocity;src_work.kit_length[step][value]=duration;}
            else {if(value>127||(src_work.step[part][step]&BR_STEP_ON))die("duplicate note");src_work.step[part][step]=BR_NOTE_STEP(value,velocity);src_work.synth_note[part][step]=value;src_work.synth_velocity[part][step]=velocity;src_work.synth_length[part][step]=duration;}
        }
        if(br_pattern_bank_paste(&source,slot,&src_live,&src_work,enc,sizeof enc)!=BR_BANK_OK)die("pattern bank exceeds device capacity");
    }
    src_song.used=source.used;
    for(unsigned i=0;i<entries;i++){unsigned slot=get(),bars=get();if(slot>=count||fm1_arr_insert(&src_song,i,slot,bars)!=FM1_ARR_OK)die("invalid arrangement");}
    if(fm1_arr_set_mode(&src_song,FM1_SONG_MODE_SONG)!=FM1_ARR_OK||fm1_arr_set_end(&src_song,loop?FM1_SONG_END_LOOP:FM1_SONG_END_STOP)!=FM1_ARR_OK)die("song mode or end");
    if(br_pattern_snapshot_apply(&src_live)||!br_capture(&session))die("capture session");
    work.pattern=&work_pattern;work.session=work_session;work.compressed=record_scratch;work.compressed_capacity=sizeof record_scratch;work.arena=staging_arena;work.arena_capacity=4096;
    if(fm1_project_source_init(&stream,(uint8_t*)&session,&source,&src_live,&src_song,valid,0,&work)!=FM1_PROJECT_OK)die("invalid project");
    int rc=fm1_codec_source_encode(fm1_project_source_read,&stream,stream.total,pair_encoded,sizeof pair_encoded,&n);
    if(rc!=FM1_CODEC_OK)die("project exceeds device storage");
    fprintf(stderr,"bank=%zu/4096 load arena (source capacity 7756), encoded=%zu/3584\n",source.size,n);
    if(fm1_compressed_project_decode(pair_encoded,n,valid,0,&work,(uint8_t*)&restored,&dest,dest_arena,4096,&dst_live,&dst_song)!=FM1_PROJECT_OK)die("device-size round trip failed");
    if(memcmp(&session,&restored,sizeof session)||dst_song.song_length!=entries||dst_song.mode!=src_song.mode||dst_song.end!=src_song.end||dest.used!=source.used)die("round trip mismatch");
    for(unsigned i=0;i<entries;i++)if(dst_song.entry[i].pattern!=src_song.entry[i].pattern||dst_song.entry[i].bars!=src_song.entry[i].bars)die("arrangement mismatch");
    for(unsigned i=0;i<count;i++){if(br_pattern_bank_copy(&source,i,&src_live,&cmp_a)||br_pattern_bank_copy(&dest,i,&dst_live,&cmp_b)||memcmp(&cmp_a,&cmp_b,sizeof cmp_a))die("pattern mismatch");}
    f=fopen(argv[1],"wb");if(!f||fwrite(pair_encoded,1,n,f)!=n||fclose(f))die("output write");return 0;
}
