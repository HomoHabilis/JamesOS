/* Shared project workflow and display for device and emulator. */
static uint32_t es_knob_active = 4u, es_knob_time, es_knob_mode;
static void es_menu_icon(int kind,int x,int y,uint16_t color) {
    if(kind==0){ /* Draw at native pixel resolution so the label stays open. */
        static const char *const disk[]={
            ".#############.", "###############", "###.......#####",
            "###.......##.##", "###.......##.##", "###.......#####",
            "###############", "##...........##", "##...........##",
            "##...........##", "##...........##", "##...........##",
            "##...........##", "###############", "###############"
        };
        for(int row=0;row<15;row++)for(int col=0;col<15;col++)
            if(disk[row][col]=='#')cv_pset(x+14-col,y+14-row,color);
    } else if(kind==1){ /* Folder and raised tab. */
        br_art_line(x,y+3,x+5,y+3,color);br_art_line(x+5,y+3,x+7,y+5,color);
        br_art_line(x+7,y+5,x+14,y+5,color);br_art_line(x,y+3,x,y+14,color);
        br_art_line(x,y+14,x+14,y+14,color);br_art_line(x+14,y+5,x+14,y+14,color);
        br_art_line(x+1,y+7,x+13,y+7,color);
    } else { /* Musical note with plus sign. */
        br_art_line(x+5,y+1,x+5,y+11,color);br_art_line(x+5,y+1,x+10,y,color);
        bw_circle(x+3,y+11,3,color,1u);br_art_line(x+10,y+7,x+16,y+7,color);
        br_art_line(x+13,y+4,x+13,y+10,color);
    }
}
static void es_button_cue(int x,int y,int w,int h) {
    br_art_rect(x+2,y,w-4,h,BR_LINE);
    br_art_rect(x,y+2,w,h-4,BR_LINE);
}
/* Project choices use the global section's accent. */
static void es_selected_row(int y) {
    br_art_rect(4,y-3,218,20,BR_GLOBAL_ACC);
    br_art_rect(222,y-1,2,16,BR_GLOBAL_ACC);
}
static void es_knob_icon(int x,int y,uint16_t color) { br_knob_icon(x,y,color); }
static void es_slot_string(char *out, int slot, const char *suffix, uint32_t cap) {
    if(cap<4u)return;
    out[0]=(char)('0'+(slot+1)/10);out[1]=(char)('0'+(slot+1)%10);out[2]=0;
    br_text_append(out,suffix,cap);
}
static void es_draw(void) {
    static const char *actions[]={"QUICKSAVE","SAVE AS","LOAD","INIT"};
    static const char *choices[]={"SAVE","DISCARD","CANCEL"};
    char text[40],number[16],slot_labels[5][20];es_record r;uint32_t p=br_ui.part;
    int first_slot=br_clip(es.slot-2,0,ES_SLOTS-5);
    if(es.mode==3)for(int i=0;i<5;i++){
        int slot=first_slot+i;
        es_slot_string(slot_labels[i],slot," ",sizeof slot_labels[i]);
        br_text_append(slot_labels[i],es_latest(slot,&r,0)?r.name:"EMPTY",sizeof slot_labels[i]);
    }
    const char *name=es.mode==1?"SAVE":es.mode==2?"SAVE FIRST?":
        es.mode==3?"CHOOSE SLOT":es.mode==4?"SAVE AS":
        es.mode==5?"OVERWRITE":"CONFIRM";
    /* Keep the cue bar intact while the body is refreshed. The last body
     * strip stops at row 215; clearing through row 239 caused visible flashes
     * before the separate cue-bar blit restored it. */
    for(int y=0;y<216;y+=40){
        uint32_t rows=(uint32_t)(216-y);if(rows>40u)rows=40u;
        cv_begin(240,rows,BR_BACKGROUND);cv_oy=-y;
        br_track_badge(6,2,p,0);
        br_header_draw_title(name);
        br_number(number,(int32_t)br_bpm);br_metronome_icon(197);
        br_widget_text(223-br_widget_text_width(number)/2,13,number,C_WHITE);
        br_battery_icon(fm1_battery_level());
        for(uint32_t k=0;k<BR_BANK_STEPS;k++){
            uint32_t at=br_ui.step_bank*BR_BANK_STEPS+k,step=br_part[p].step[at];
            uint32_t active=br_is_kit(br_part[p].sound.preset)?(BR_STEP_HIT(step)>>br_ui.lane)&1u:(step&BR_STEP_ON)!=0;
            int32_t x=8+(int32_t)k*14;
            br_art_rect(x,26,11,4,at>=br_part[p].length?RGB(16,19,20):active?br_track_color(p):BR_LINE);
            if(at==br_ui.cursor)br_art_rect(x,31,11,1,C_WHITE);
            if(br_playing&&at==br_step_pos[p])br_art_rect(x+3,24,5,1,C_WHITE);
        }
        /* The shared header and sequence strip stay fixed; move the menu body. */
        cv_oy=-y-8;
        if(es.mode==1 || es.mode==2 || es.mode==3){
            int top=es.mode==3?65:113, bottom=es.mode==3?195:es.mode==1?209:201;
            cv_line(5,top,64,top,C_WHITE);
            cv_line(3,top+2,3,bottom,C_WHITE);
            cv_pset(4,top+1,C_WHITE);cv_pset(65,top+1,C_WHITE);
            cv_line(66,top+2,66,top+14,C_WHITE);
            cv_pset(65,top+15,C_WHITE);cv_line(3,top+16,64,top+16,C_WHITE);
            uint32_t active=es_knob_active==0u && es_knob_mode==(uint32_t)es.mode;
            if(active)cv_rect(4,top+1,62,15,BR_GLOBAL_ACC);
            es_knob_icon(8,top+3,active?C_BLACK:C_WHITE);
            br_widget_text(24,top+5,"KNOB1",active?C_BLACK:C_WHITE);
        }
        if(es.mode==1){
            for(int i=0;i<4;i++){
                int selected=i==es.action, row=136+i*19;
                if(selected)es_selected_row(row);
                es_menu_icon(i<2?0:i-1,12,row,selected?C_BLACK:C_WHITE);
                br_widget_text_2x(36,row,actions[i],selected?C_BLACK:C_WHITE);
            }
        } else if(es.mode==2){
            br_widget_text(92,117,"SAVE CURRENT?",BR_DIM);
            for(int i=0;i<3;i++){
                int selected=i==es.choice, row=136+i*23;
                if(selected)es_selected_row(row);
                br_widget_text_2x(12,row,choices[i],selected?C_BLACK:C_WHITE);
            }
        } else if(es.mode==3){
            br_widget_text(92,69,es.slot_purpose==1?"LOAD SLOT":"SAVE SLOT",BR_DIM);
            es_slot_string(text,es.slot,"/12",sizeof text);br_widget_text(195,69,text,BR_DIM);
            for(int i=0;i<5;i++){
                int selected=first_slot+i==es.slot, row=88+i*22;
                if(selected)es_selected_row(row);
                br_widget_text_2x(12,row,slot_labels[i],selected?C_BLACK:C_WHITE);
            }
        } else if(es.mode==4){
            int cell_x=8+es.cursor*14, center=cell_x+7;
            int tab_x=cell_x;
            br_widget_text_2x(8,47,"NAME",C_WHITE);
            cv_line(8,68,71,68,C_WHITE);cv_line(8,68,8,84,C_WHITE);
            cv_line(71,68,71,84,C_WHITE);cv_line(8,84,176,84,C_WHITE);
            uint32_t active1=es_knob_active==1u && es_knob_mode==4u;
            uint32_t active2=es_knob_active==0u && es_knob_mode==4u;
            if(active1)cv_rect(9,69,62,15,BR_GLOBAL_ACC);
            es_knob_icon(12,71,active1?C_BLACK:C_WHITE);
            br_widget_text(28,73,"KNOB2",active1?C_BLACK:C_WHITE);
            br_art_rect(cell_x,88,14,18,BR_GLOBAL_ACC);
            for(int i=0;i<12;i++){
                char glyph[2]={es.name[i],0};
                br_widget_text_2x(8+i*14+1,90,glyph,i==es.cursor?C_BLACK:C_WHITE);
            }
            cv_line(center,106,center,110,BR_GLOBAL_ACC);
            cv_line(tab_x,110,tab_x+63,110,BR_GLOBAL_ACC);cv_line(tab_x,110,tab_x,126,BR_GLOBAL_ACC);
            cv_line(tab_x+63,110,tab_x+63,126,BR_GLOBAL_ACC);cv_line(tab_x,126,tab_x+63,126,BR_GLOBAL_ACC);
            if(active2)cv_rect(tab_x+1,111,62,15,BR_GLOBAL_ACC);
            es_knob_icon(tab_x+4,113,active2?C_BLACK:BR_GLOBAL_ACC);
            br_widget_text(tab_x+20,115,"KNOB1",active2?C_BLACK:BR_GLOBAL_ACC);
        } else if(es.mode==5){
            br_widget_text_2x(8,47,"OVERWRITE?",C_WHITE);br_text_copy(text,"SLOT ",sizeof text);es_slot_string(number,es.slot,"",sizeof number);br_text_append(text,number,sizeof text);br_widget_text_2x(8,82,text,BR_ACC);
        } else if(es.mode==6){
            br_widget_text_2x(8,47,"SAVE?",C_WHITE);br_text_copy(text,"SLOT ",sizeof text);es_slot_string(number,es.slot,"",sizeof number);br_text_append(text,number,sizeof text);br_widget_text(8,80,text,BR_DIM);
            br_widget_text_2x(8,99,es.name,C_WHITE);
        }
        cv_oy=0;cv_blit(0,(uint32_t)y);
    }
    br_draw_cue_bar(es.mode==1?2u:es.mode==3?10u:es.mode==4?9u:es.mode==5?5u:es.mode>=4?4u:3u,1u);
}
static void es_commit(void) {
    if(es.pending==1 && es.slot==es.target && es.current<0){es_error("CHOOSE DIFFERENT SLOT");return;}
    if(es_write(es.slot,es.name)){if(es.pending)es_finish_pending();else es_close();}
}
static void es_confirm(void) {
    es_record r;
#if defined(BR_PATTERN_PROJECT_UI_TARGET) && BR_PATTERN_PROJECT_UI_TARGET
    if(es_project_fault){es_error("RECOVERY NEEDED");return;}
    if(!br_pp_target_ready() || es_project_boot_state!=1u){es_error(es_project_boot_state==2u?"RECOVERY FAILED":"STORE LOCKED");return;}
#endif
    if(es.mode==1){
        if(es.action==0){es_quick_save();return;}
        if(es.action<3){es.slot_purpose=es.action-1;es.mode=3;es.error[0]=0;return;}
        es.pending=2;es.choice=0;es.mode=2;es.error[0]=0;return;
    }
    if(es.mode==2){
        if(es.choice==2){es_close();return;}
        if(es.choice==1){es_finish_pending();return;}
        if(es.current>=0 && es_latest(es.current,&r,0)){
            if(es_write(es.current,r.name))es_finish_pending();return;
        }
        es.mode=3;es.slot_purpose=0;es.slot=0;return;
    }
    if(es.mode==3){
        if(!es_slot_ready(es.slot)){
            es_error(es_slot_retry(es.slot)?"RETRYING SLOT":br_playing?"STOP PLAYBACK":"SCANNING SLOTS");return;
        }
        if(es.slot_purpose==1){
            if(!es_slot_loadable(es.slot)){es_error("SLOT UNAVAILABLE");return;}
            es.target=es.slot;es.pending=1;es.choice=0;es.mode=2;es.error[0]=0;return;
        }
        es_name_begin();return;
    }
    if(es.mode==4){if(es_latest(es.slot,&r,0)&&(es.slot!=es.current||es_directory[es.slot].state==3)){es.mode=5;return;}es_commit();return;}
    if(es.mode==5)es_commit();
    else if(es.mode==6){es.mode=4;es_confirm();}
}
static void es_record_toggle(void) {
    BR_FOREGROUND_MUTATE({br_recording^=1u;if(!br_recording)br_record_note_cancel_all();});
    br_message(br_recording?"RECORD ARMED":"RECORD OFF");
}
static int es_input(uint32_t *buttons,uint32_t *released,uint32_t keys) {
    uint32_t down=*buttons,up=*released;
    int mode_at_start=es.mode;
    const uint32_t modes=(1u<<BTN_HOME)|(1u<<BTN_ENV)|(1u<<BTN_FX)|(1u<<BTN_GLO)|
        (1u<<BTN_SEQ)|(1u<<BTN_ARP)|(1u<<BTN_SEL)|(1u<<BTN_LFO)|(1u<<BTN_EDIT);
    if(es.mode && (down&modes)){
        es_close();
        /* A simultaneous mode change must never commit a save. */
        *buttons&=~(1u<<BTN_SAVE);down&=~(1u<<BTN_SAVE);
    }
    if(down&(1u<<BTN_REC)){es.rec_pending=1;es.rec_used=0;*buttons&=~(1u<<BTN_REC);}
    if((down&(1u<<BTN_SAVE)) && (es.rec_pending || (fm1_in.buttons&(1u<<BTN_REC)))) {
        es.rec_used=1;*buttons&=~(1u<<BTN_SAVE);
        if(!es.mode)es_quick_save();
    } else if(down&(1u<<BTN_SAVE)) {
        *buttons&=~(1u<<BTN_SAVE);if(!es.mode)es_open();else if(es.mode==4)es.mode=6;else es_confirm();
    }
    if(up&(1u<<BTN_REC)){
        *released&=~(1u<<BTN_REC);
        if(es.rec_pending&&!es.rec_used&&!es.mode)es_record_toggle();
        es.rec_pending=es.rec_used=0;
    }
    if(!es.mode)return 0;
    if(!(down&(1u<<BTN_SAVE))){
        if(down&(1u<<BTN_OCT_DOWN)){
            if(es.mode==2){int loading=es.pending==1;es.mode=loading?3:1;es.slot_purpose=loading?1:0;es.pending=0;es.error[0]=0;}
            else if(es.mode==3){es.mode=es.pending?2:1;es.error[0]=0;}
            else if(es.mode==4){es.mode=es.name_from;es.error[0]=0;}
            else if(es.mode==5){es.mode=4;es.error[0]=0;}
            else if(es.mode==6){es.mode=4;es.error[0]=0;}
        }
    }
    if(!es.mode)return 1;
    int a=fm1_enc_take(ENC_KNOB1), b=fm1_enc_take(ENC_KNOB2);
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    a+=br_project_diag_knob1;b+=br_project_diag_knob2;
    br_project_diag_knob1=br_project_diag_knob2=0;
#endif
    if(es_knob_active<4u && (es_knob_mode!=(uint32_t)es.mode || fm1_ms-es_knob_time>=1200u)) {
        es_knob_active=4u;br_ui.redraw=1;
    }
    if(es.mode==mode_at_start && ((a && es.mode<=4) || (b && es.mode==4))) {
        es_knob_active=(b && es.mode==4)?1u:0u;
        es_knob_mode=(uint32_t)es.mode;es_knob_time=fm1_ms;
    }
    if(a||b||down||up)br_ui.redraw=1;
    if(a||b||(down&((1u<<BTN_OCT_DOWN)|(1u<<BTN_OCT_UP))))es.notice[0]=0;
    (void)fm1_enc_take(ENC_KNOB3);(void)fm1_enc_take(ENC_KNOB4);(void)fm1_enc_take(ENC_SELECT);(void)fm1_enc_take(ENC_ALGORITHM);(void)fm1_enc_take(ENC_PRESETS);
    if(es.mode==1){es.action=br_clip(es.action+a,0,3);}
    else if(es.mode==2)es.choice=br_clip(es.choice+a,0,2);
    else if(es.mode==3)es.slot=br_clip(es.slot+a,0,11);
    else if(es.mode==4 && mode_at_start==4){
        es.cursor=br_clip(es.cursor+b,0,11);
        if(a){const char *alphabet=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";int len=0,i=0;while(alphabet[len])len++;while(i<len && alphabet[i]!=es.name[es.cursor])i++;if(i==len)i=0;i=(i+a)%len;if(i<0)i+=len;es.name[es.cursor]=alphabet[i];}
    }
    /* Publish each LED's final SAVE-screen level once per update. The LED
     * scanner can interrupt here, so never briefly restore the underlying page. */
    br_ui.previous_keys=keys;br_leds_mode((uint32_t)es.mode);
    return 1;
}
