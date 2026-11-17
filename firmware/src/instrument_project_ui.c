/* Shared project workflow and display for device and emulator. */
static uint32_t es_knob_active = 4u, es_knob_time, es_knob_mode;
static void es_menu_icon(int kind,int x,int y,uint16_t color) {
    { /* Disk (shutter, label, clipped corner), folder with a raised tab, or a note with a plus: fine strokes. */
        static const int8_t seg[24][4]={
            {0,0,12,0},{12,0,14,2},{14,2,14,14},{14,14,0,14},{0,14,0,0},{3,0,3,4},{3,4,10,4},{10,4,10,0},{8,1,8,3},
            {2,14,2,8},{2,8,12,8},{12,8,12,14},
            {0,3,5,3},{5,3,7,5},{7,5,14,5},{0,3,0,14},{0,14,14,14},{14,5,14,14},{1,7,13,7},
            {5,1,5,11},{5,1,10,0},{10,7,16,7},{13,4,13,10}
        };
        int i0=kind==0?0:kind==1?12:19, i1=kind==0?12:kind==1?19:23;
        for(int i=i0;i<i1;i++)
            hd_line_q8((x+seg[i][0])*256+128,(y+seg[i][1])*256+128,(x+seg[i][2])*256+128,(y+seg[i][3])*256+128,384u,color);
        if(kind==2)hd_circle_q8((x+3)*256+128,(y+11)*256,3*256,0,color);
        return;
    }
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
    }
}
static void es_button_cue(int x,int y,int w,int h) {
    br_art_rect(x+2,y,w-4,h,BR_LINE);
    br_art_rect(x,y+2,w,h-4,BR_LINE);
}
/* Project choices use the global section's accent. */
static void es_selected_row(int y) {
    hds_bar(y,BR_GLOBAL_ACC);
}
static void es_knob_icon(int x,int y,uint16_t color) { br_knob_icon(x,y,color); }
static void es_slot_string(char *out, int slot, const char *suffix, uint32_t cap) {
    if(cap<4u)return;
    out[0]=(char)('0'+(slot+1)/10);out[1]=(char)('0'+(slot+1)%10);out[2]=0;
    br_text_append(out,suffix,cap);
}
static void es_draw_cue(void) {   /* the cue bar alone: its slides while a message shows */
    br_draw_cue_bar(es.mode==1?2u:es.mode==3?10u:es.mode==4?9u:es.mode==5?5u:es.mode>=4?4u:3u,0u);
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
    /* Drawn only when something it shows changed (h), in 10-row bands of which only those whose pixels
     * changed go out (br_crc_blit), a pass's worth at a time (br_band). The header and strip are as on the
     * main screen; entering from it is a new epoch, leaving it a layout change. */
    static uint32_t es_old; static uint8_t es_next;
    uint32_t h=((((((uint32_t)es.mode*8u+(uint32_t)es.action)*8u+(uint32_t)es.choice)*16u+(uint32_t)es.slot)*4u+
        (uint32_t)es.slot_purpose)*16u+(uint32_t)es.cursor)*8u+es_knob_active*2u+(es_knob_mode==(uint32_t)es.mode);
    h=h*31u+br_hash_text(es.name)+br_hash_text(name)*7u;
    if(es.mode==3)for(int i=0;i<5;i++)h=h*31u+br_hash_text(slot_labels[i]);
    if(br_draw_layout_old!=0xffffffffu){br_draw_layout_old=0xffffffffu;br_pass.epoch+=0x9e3779b9u;}
    /* Keep the cue bar intact while the body is refreshed. The last body
     * strip stops at row 215; clearing through row 239 caused visible flashes
     * before the separate cue-bar blit restored it. */
    /* The header and strip as on the main screen, drawn by it (br_draw_top: a playing pattern moves only
     * the metronome's arm and the playhead); the body below them in bands from h. */
    br_top.name=name;br_top.battery=fm1_battery_level();br_top.save=1u;
    br_draw_top(0u);
    for(int y=32;y<216;y=y<40?40:y+10){   /* rows 32..39, then 10-row bands from row 40 */
        uint32_t rows=y<40?8u:(uint32_t)(216-y);if(rows>10u)rows=10u;
        if(!br_band(&es_old,&es_next,h,(uint32_t)(y-30)/10u,0u))continue;
        cv_begin(240,rows,BR_BACKGROUND);cv_oy=-y;
        /* The shared header and sequence strip stay fixed; move the menu body. */
        cv_oy=-y-8;
        if(es.mode==1 || es.mode==2 || es.mode==3){
            int top=es.mode==3?65:113, bottom=es.mode==3?195:es.mode==1?209:201;
            uint32_t active=es_knob_active==0u && es_knob_mode==(uint32_t)es.mode;
            hds_chip(3,top,C_WHITE,BR_GLOBAL_ACC,active);
            hds_rule(top+20,bottom);
            es_knob_icon(8,top+3,active?C_BLACK:C_WHITE);
            br_widget_text(24,top+5,BR_PANEL_KNOB1,active?C_BLACK:C_WHITE);
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
            es_slot_string(text,es.slot,"/12",sizeof text);
            br_widget_text(br_widget_text_reright(195,text,1),69,text,BR_DIM);   /* right edge where the pixel font ends */
            for(int i=0;i<5;i++){
                int selected=first_slot+i==es.slot, row=88+i*22;
                if(selected)es_selected_row(row);
                br_widget_text_2x(12,row,slot_labels[i],selected?C_BLACK:C_WHITE);
            }
        } else if(es.mode==4){
            int cell_x=8+es.cursor*14, center=cell_x+7;
            int tab_x=cell_x;
            br_widget_text_2x(8,47,"NAME",C_WHITE);
            uint32_t active1=es_knob_active==1u && es_knob_mode==4u;
            uint32_t active2=es_knob_active==0u && es_knob_mode==4u;
            /* KNOB2 tab on the field's rail; a quiet cell per character, the cursor's cell raised in gold. */
            hd_line(8,84,176,84,RGB(96,104,108));
            hds_chip(8,68,C_WHITE,BR_GLOBAL_ACC,active1);
            for(int i=0;i<12;i++)
                if(i==es.cursor)hd_bevel(8+i*14,88,13,18,3,BR_GLOBAL_ACC);
                else hd_rrect_grad(8+i*14,88,13,18,3,RGB(24,28,31),RGB(11,13,15));
            es_knob_icon(12,71,active1?C_BLACK:C_WHITE);
            br_widget_text(28,73,BR_PANEL_KNOB2,active1?C_BLACK:C_WHITE);
            for(int i=0;i<12;i++){
                char glyph[2]={es.name[i],0};
                /* One character centred in each 13 px cell, whatever its width. */
                br_widget_text_2x(8+i*14+(13-br_widget_text_width_scaled(glyph,2))/2,90,glyph,i==es.cursor?C_BLACK:C_WHITE);
            }
            hd_line(center,106,center,110,BR_GLOBAL_ACC);
            hds_chip(tab_x,110,BR_GLOBAL_ACC,BR_GLOBAL_ACC,active2);
            es_knob_icon(tab_x+4,113,active2?C_BLACK:BR_GLOBAL_ACC);
            br_widget_text(tab_x+20,115,BR_PANEL_KNOB1,active2?C_BLACK:BR_GLOBAL_ACC);
        } else if(es.mode==5){
            br_widget_text_2x(8,47,"OVERWRITE?",C_WHITE);br_text_copy(text,"SLOT ",sizeof text);es_slot_string(number,es.slot,"",sizeof number);br_text_append(text,number,sizeof text);br_widget_text_2x(8,82,text,BR_ACC);
        } else if(es.mode==6){
            br_widget_text_2x(8,47,"SAVE?",C_WHITE);br_text_copy(text,"SLOT ",sizeof text);es_slot_string(number,es.slot,"",sizeof number);br_text_append(text,number,sizeof text);br_widget_text(8,80,text,BR_DIM);
            br_widget_text_2x(8,99,es.name,C_WHITE);
        }
        cv_oy=0;br_crc_blit(0,(uint32_t)y,&br_band_crc[(y-30)/10]);
    }
    if((int32_t)(br_ui.message_until-fm1_ms)<=0)es_draw_cue();   /* while a message shows, its 32 ms slides own the cue */
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
            if(!es_slot_loadable(es.slot)){es_error(es_directory[es.slot].state==3&&es_directory[es.slot].old?"OLD PROJECT - NOT LOADED":"SLOT UNAVAILABLE");return;}
            es.target=es.slot;es.pending=1;es.choice=0;es.mode=2;es.error[0]=0;return;
        }
        es_name_begin();return;
    }
#if BR_PRODUCT_SMK37
    if(es.mode==4){
        if(es_latest(es.slot,&r,0) && (es.slot!=es.current
#if !defined(DINGHY_EMULATOR_SAVE)
            || es_directory[es.slot].state==3
#endif
        )){es.mode=5;return;}
        es_commit();return;
    }
#else
    if(es.mode==4){if(es_latest(es.slot,&r,0)&&(es.slot!=es.current||es_directory[es.slot].state==3)){es.mode=5;return;}es_commit();return;}
#endif
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
