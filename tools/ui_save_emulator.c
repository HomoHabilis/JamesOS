/* Emulator-only SAVE workflow; persistent project files are local, not FM-1 flash. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define ES_SLOTS 12
#define ES_MAGIC 0x31565045u
#define ES_PATH "build/ui-emulator/projects"
typedef struct { uint32_t magic, slot, generation; char name[13]; br_session_t session; uint32_t crc; } es_record;
static struct {
    int mode, action, slot, slot_purpose, current, pending, target, choice, cursor, name_from, rec_pending, rec_used;
    char name[13], error[32], notice[32];
    br_session_t factory;
} es = {.current = -1};
static uint32_t es_crc(const void *v, size_t n) {
    const uint8_t *p = (const uint8_t *)v; uint32_t h = 2166136261u;
    while (n--) { h ^= *p++; h *= 16777619u; } return h;
}
typedef struct { uint32_t magic, slot, session_crc, checksum; } es_identity;
static int es_save_identity(const br_session_t *session) {
    es_identity id={ES_MAGIC,(uint32_t)(es.current+1),es_crc(session,sizeof *session),0};
    id.checksum=es_crc(&id,offsetof(es_identity,checksum));
    const char *tmp=ES_PATH "/identity.tmp", *path=ES_PATH "/identity.bin";
    FILE *f=fopen(tmp,"wb");if(!f)return 0;
    int ok=fwrite(&id,1,sizeof id,f)==sizeof id;if(fclose(f))ok=0;
    if(!ok||rename(tmp,path)){remove(tmp);return 0;}return 1;
}
static void es_restore_identity(const br_session_t *session) {
    es_identity id;FILE *f=fopen(ES_PATH "/identity.bin","rb");if(!f)return;
    int ok=fread(&id,1,sizeof id,f)==sizeof id && fgetc(f)==EOF;fclose(f);
    if(ok && id.magic==ES_MAGIC && id.slot<=ES_SLOTS &&
       id.checksum==es_crc(&id,offsetof(es_identity,checksum)) &&
       id.session_crc==es_crc(session,sizeof *session)) es.current=(int)id.slot-1;
}
static void es_path(char *out, size_t n, int slot, int copy) {
    snprintf(out, n, ES_PATH "/slot-%02d-%d.bin", slot+1, copy);
}
static int es_read(int slot, int copy, es_record *r) {
    char path[128]; es_path(path,sizeof path,slot,copy);
    FILE *f=fopen(path,"rb"); if (!f) return 0;
    int ok=fread(r,1,sizeof *r,f)==sizeof *r && fgetc(f)==EOF; fclose(f);
    return ok && r->magic==ES_MAGIC && r->slot==(uint32_t)slot && r->name[12]==0 &&
        r->crc==es_crc(r,offsetof(es_record,crc)) && (br_session_v12_valid(&r->session) || br_session_v13_valid(&r->session) || br_session_v14_valid(&r->session) || br_session_v15_valid(&r->session) || br_session_v16_valid(&r->session));
}
static int es_latest(int slot, es_record *out, int *copy) {
    es_record a,b; int va=es_read(slot,0,&a),vb=es_read(slot,1,&b);
    if(!va&&!vb) return 0;
    int use=vb && (!va || (int32_t)(b.generation-a.generation)>0);
    if(out)*out=use?b:a; if(copy)*copy=use; return 1;
}
static int es_slot_ready(int slot) { (void)slot; return 1; }
static int es_slot_loadable(int slot) { return es_latest(slot,0,0); }
static int es_slot_retry(int slot) { (void)slot; return 0; }
static void es_error(const char *s) { es.notice[0]=0; br_text_copy(es.error,s,sizeof es.error); br_message(s); br_ui.message_until+=2000u; }
static int es_write(int slot,const char *name) {
    es_record old,r,check; int active=0,exists=es_latest(slot,&old,&active),dest=exists?!active:0;
    if(br_playing){es_error("STOP PLAYBACK FIRST");return 0;}
    memset(&r,0,sizeof r);r.magic=ES_MAGIC;r.slot=(uint32_t)slot;r.generation=exists?old.generation+1u:1u;
    br_text_copy(r.name,name,sizeof r.name);
    if(!br_capture(&r.session)){es_error("INVALID PROJECT");return 0;}
    r.crc=es_crc(&r,offsetof(es_record,crc));
    char path[128],tmp[140];es_path(path,sizeof path,slot,dest);snprintf(tmp,sizeof tmp,"%s.tmp",path);
    FILE *f=fopen(tmp,"wb");if(!f){es_error("SAVE FAILED");return 0;}
    int ok=fwrite(&r,1,sizeof r,f)==sizeof r;if(fclose(f))ok=0;
    if(!ok||rename(tmp,path)){remove(tmp);es_error("SAVE FAILED");return 0;}
    if(!es_read(slot,dest,&check)||memcmp(&check,&r,sizeof r)){es_error("VERIFY FAILED");return 0;}
    es.current=slot;es.error[0]=0;return 1;
}
static void es_close(void) { es.mode=0;es.pending=0;es.error[0]=0;es.notice[0]=0;br_draw_layout_old=0xFFFFFFFFu;br_ui.redraw=1; }
static void es_finish_pending(void) {
    if(br_playing){es_error("STOP PLAYBACK FIRST");return;}
    if(es.pending==1){es_record r;if(!es_latest(es.target,&r,0)||!br_restore(&r.session)){es_error("LOAD FAILED");return;}es.current=es.target;}
    else if(es.pending==2){if(!br_restore(&es.factory)){es_error("INIT FAILED");return;}es.current=-1;}
    es_close();
}
static void es_name_begin(void) {
    es_record r;memset(es.name,0,sizeof es.name);
    es.name_from=es.mode;
    if(es_latest(es.slot,&r,0))br_text_copy(es.name,r.name,sizeof es.name);
    else snprintf(es.name,sizeof es.name,"PROJECT %02d",es.slot+1);
    for(int i=(int)strlen(es.name);i<12;i++)es.name[i]=' ';
    es.name[12]=0;es.cursor=0;es.mode=4;es.error[0]=0;
}
static void es_open(void) {
    for(uint32_t k=0;k<27;k++)if(br_ui.held_note[k])br_key_up(k);
    if(br_step_press_pending() || br_ui.step_edit_active)br_step_edit_end();
    es.mode=1;es.action=0;es.slot=es.current>=0?es.current:0;es.slot_purpose=0;es.pending=0;es.error[0]=0;es.notice[0]=0;
}
static void es_quick_save(void) {
    if(es.current<0){es_open();es.mode=3;return;}
    es_record r;if(!es_latest(es.current,&r,0)){es_open();es.mode=3;es_error("SELECT SLOT");return;}
    if(es_write(es.current,r.name)){
        if(es.mode==1)es_close();
        br_message("PROJECT SAVED");
    }
}
#include "../firmware/src/instrument_project_ui.c"
