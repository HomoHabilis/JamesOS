#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
int main(void)
{
    br_session_t saved;
    br_app_init(); host_after_intro();
    br_ui.part=0; br_ui.page=BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME]=0;
    br_buttons(1u<<BTN_HOME); HD_CHECK(br_ui.screen[BR_PAGE_HOME]==1u,"HOME filter second");
    br_buttons(1u<<BTN_HOME); HD_CHECK(br_ui.screen[BR_PAGE_HOME]==0u,"HOME normal pages wrap");
    br_open_home_settings(); HD_CHECK(br_ui.screen[BR_PAGE_HOME]==2u,"HOME deep menu starts at mod depths");
    HD_CHECK(!strcmp(br_header_title(),"MOD DEPTHS"),"HOME title");
    br_control_t c=br_control(0u); HD_CHECK(!strcmp(c.label,"TIMBRE"),"mod depths label");
    uint32_t before=br_part[0].sound.timbre_mod; br_control_turn(0u,-1);
    HD_CHECK(br_part[0].sound.timbre_mod+1u==before,"mod depths edit");
    br_buttons(1u<<BTN_HOME); HD_CHECK(br_ui.screen[BR_PAGE_HOME]==3u,"HOME deep second page");
    HD_CHECK(!strcmp(br_header_title(),"VOICE"),"voice page title");
    HD_CHECK(!strcmp(br_control(0u).label,"PORTA TIME") && !strcmp(br_control(1u).text,"OFF") &&
             !strcmp(br_control(2u).text,"ENVELOPE") && !br_control(3u).label[0],"voice controls and inactive fourth");
    /* Switching AMP MODE must repaint its card identically to a clean frame. */
    {
        static uint16_t envelope[60u * 70u], gate[60u * 70u];
        br_ui.redraw=1u; br_draw();
        for (uint32_t y=0; y<70u; ++y) memcpy(envelope+y*60u, fb+(y+BR_PARAM_STANDARD_BODY_Y+BR_PARAM_ICON_Y)*240u+120u, 60u*sizeof(uint16_t));
        br_control_turn(2u,1); br_draw();
        for (uint32_t y=0; y<70u; ++y) memcpy(gate+y*60u, fb+(y+BR_PARAM_STANDARD_BODY_Y+BR_PARAM_ICON_Y)*240u+120u, 60u*sizeof(uint16_t));
        HD_CHECK(memcmp(envelope,gate,sizeof gate), "AMP MODE icons differ");
        br_ui.redraw=1u; br_draw();
        for (uint32_t y=0; y<70u; ++y)
            HD_CHECK(!memcmp(gate+y*60u,fb+(y+BR_PARAM_STANDARD_BODY_Y+BR_PARAM_ICON_Y)*240u+120u,60u*sizeof(uint16_t)), "LPG icon partial redraw matches full");
        br_control_turn(2u,-1); br_draw();
        for (uint32_t y=0; y<70u; ++y)
            HD_CHECK(!memcmp(envelope+y*60u,fb+(y+BR_PARAM_STANDARD_BODY_Y+BR_PARAM_ICON_Y)*240u+120u,60u*sizeof(uint16_t)), "Envelope icon restored after LPG");
    }
    br_control_turn(0u,50); br_control_turn(1u,1); br_control_turn(2u,1);
    HD_CHECK(br_part[0].sound.portamento==50u && br_part[0].sound.glide_mode==1u && br_part[0].sound.amp_mode==1u,
             "voice settings change selected track");
    HD_CHECK(!strcmp(br_control(1u).text,"LEGATO") && !strcmp(br_control(2u).text,"LPG") && !strcmp(br_control(3u).label,"GATE LENGTH"),"voice choices");
    br_control_turn(3u, 15); HD_CHECK(br_part[0].sound.lpg_gate==15u,"LPG gate edited");
    br_ui.page=BR_PAGE_ENV; br_ui.screen[BR_PAGE_ENV]=0u;
    HD_CHECK(!br_control(3u).label[0],"amp release is inactive in LPG mode");
    br_ui.page=BR_PAGE_HOME;
    br_ui.part=1u;
    HD_CHECK(br_control(0u).value==0 && !strcmp(br_control(1u).text,"OFF") &&
             !strcmp(br_control(2u).text,"ENVELOPE"),"other synth starts with its own voice settings");
    br_control_turn(0u,25); br_control_turn(1u,2);
    br_control_turn(3u,10); HD_CHECK(!br_part[1].sound.lpg_gate,"gate knob inactive in envelope mode");
    br_ui.part=0u;
    HD_CHECK(br_control(0u).value==50 && !strcmp(br_control(1u).text,"LEGATO") &&
             !strcmp(br_control(2u).text,"LPG"),"returning to T1 restores its own voice settings");
    HD_CHECK(br_capture(&saved),"save independent voice settings");
    br_part[0].sound.portamento=br_part[1].sound.portamento=0u;
    HD_CHECK(br_restore(&saved),"load independent voice settings");
    HD_CHECK(br_part[0].sound.portamento==50u && br_part[0].sound.glide_mode==1u && br_part[0].sound.amp_mode==1u && br_part[0].sound.lpg_gate==15u &&
             br_part[1].sound.portamento==25u && br_part[1].sound.glide_mode==2u && br_part[1].sound.amp_mode==0u,
             "voice settings survive a project round trip independently");
    br_ui.part=3u; HD_CHECK(!br_control(0u).label[0] && !br_control(1u).label[0] && !br_control(2u).label[0],"kit voice controls inactive");
    {
        static uint16_t shown[240u * 20u];
        const char *message = "NO SETTINGS FOR DRUMS";
        br_ui.redraw=1u; br_draw(); memcpy(shown, fb + 112u * 240u, sizeof shown);
        cv_begin(240, 20, BR_BACKGROUND);
        br_widget_text_2x((240 - 2 * br_widget_text_width(message)) / 2, 3, message, BR_DIM);
        cv_blit(0, 112);
        HD_CHECK(!memcmp(shown, fb + 112u * 240u, sizeof shown), "drum voice page shows no-settings message");
    }
    br_ui.part=0u;
    br_buttons(1u<<BTN_HOME); HD_CHECK(br_ui.screen[BR_PAGE_HOME]==2u,"HOME deep pages wrap");
    br_open_home_settings(); HD_CHECK(br_ui.screen[BR_PAGE_HOME]==0u,"HOME long press returns to sound");
#if BR_PRODUCT_SMK37
    /* SMK-37: EDIT has no KIT LEVEL; FADER BANK opens it and EDIT leaves it for PATCH 1. */
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_ui.screen[BR_PAGE_ROUTE]==0u && !br_gate_page(),"EDIT enters patch 1");
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_ui.screen[BR_PAGE_ROUTE]==1u,"EDIT enters patch 2");
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_ui.screen[BR_PAGE_ROUTE]==0u,"EDIT cycles without MOD DEPTHS or KIT LEVEL");
    br_smk_fader_bank_select(1u); HD_CHECK(br_gate_page(),"FADER BANK enters kit level");
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_ui.screen[BR_PAGE_ROUTE]==0u,"EDIT leaves kit level for patch 1");
#else
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_gate_page(),"EDIT enters kit level");
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_ui.screen[BR_PAGE_ROUTE]==0u,"EDIT enters patch 1");
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_ui.screen[BR_PAGE_ROUTE]==1u,"EDIT enters patch 2");
    br_buttons(1u<<BTN_EDIT); HD_CHECK(br_gate_page(),"EDIT cycles without MOD DEPTHS");
#endif
    br_ui.page=BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE]=0; br_ui.part=0;
    br_part[0].mod.route[BR_ROUTES].amount=20; br_part[1].mod.route[BR_ROUTES].amount=60;
    br_control_turn(2u,1);
    HD_CHECK(br_part[0].mod.route[BR_ROUTES].amount==21 && br_part[1].mod.route[BR_ROUTES].amount==60,"patch edit affects selected track only");
    for (uint32_t p=0;p<BR_PARTS;p++) for (uint32_t i=0;i<2;i++) {
        volatile br_route_config_t *x=&br_part[p].mod.route[BR_ROUTES+i];
        x->source=BR_SOURCE_LFO1+(p%2u); x->target=BR_TARGET_TIMBRE+i; x->amount=(int32_t)(-50+(int)p*8+(int)i); x->enabled=1;
        br_part[p].mod.route[7].source=BR_SOURCE_ENV; br_part[p].mod.route[7].target=BR_TARGET_LEVEL; br_part[p].mod.route[7].amount=p+10; br_part[p].mod.route[7].enabled=1;
    }
    HD_CHECK(br_capture(&saved)&&saved.b[4]==BR_V17_VERSION,"capture new format");
    for (uint32_t p=0;p<BR_PARTS;p++) for (uint32_t i=0;i<2;i++) br_part[p].mod.route[BR_ROUTES+i].amount=0;
    HD_CHECK(br_restore(&saved),"restore new format");
    for (uint32_t p=0;p<BR_PARTS;p++) for (uint32_t i=0;i<2;i++) {
        volatile br_route_config_t *x=&br_part[p].mod.route[BR_ROUTES+i];
        HD_CHECK(x->source==BR_SOURCE_LFO1+(p%2u)&&x->target==BR_TARGET_TIMBRE+i&&x->amount==-50+(int)p*8+(int)i&&x->enabled,"per-track patch %u/%u",p,i);
        HD_CHECK(br_part[p].mod.route[7].enabled && br_part[p].mod.route[7].amount==(int)p+10,"legacy track route preserved");
    }
    br_master.global_route[0]=(br_route_config_t){BR_SOURCE_ENV,BR_TARGET_LEVEL,-42,1};
    br_master.global_route[1]=(br_route_config_t){BR_SOURCE_LFO2,BR_TARGET_TIMBRE,31,1};
    HD_CHECK(br_capture(&saved),"capture legacy fixture"); saved.b[4]=17;
    for (uint32_t p=0;p<BR_PARTS;p++) for(uint32_t i=0;i<8;i++) saved.b[br_v17_wire_at(BR_V17_ROW(T,route_enabled),p,i)]&=1u;
    HD_CHECK(br_restore(&saved),"restore legacy format");
    for (uint32_t p=0;p<BR_PARTS;p++) {
        HD_CHECK(br_part[p].mod.route[BR_ROUTES].source==BR_SOURCE_ENV&&br_part[p].mod.route[BR_ROUTES].amount==-42,"legacy patch 1 cloned");
        HD_CHECK(br_part[p].mod.route[BR_ROUTES+1].source==BR_SOURCE_LFO2&&br_part[p].mod.route[BR_ROUTES+1].amount==31,"legacy patch 2 cloned");
        HD_CHECK(br_part[p].mod.route[7].enabled&&br_part[p].mod.route[7].amount==(int)p+10,"legacy hidden route preserved");
    }
    /* LOOP ON shows the repeating envelope without the former white return arrow. */
    cv_begin(60u, 60u, BR_BACKGROUND);
    { uint32_t env[4]={0}; bw_env_mode(0,0,60u,127u,0u,env,0u); }
    for (uint32_t y=0;y<14u;y++) for(uint32_t x=0;x<60u;x++)
        HD_CHECK(cv_px[y*60u+x]==BR_BACKGROUND,"loop arrow removed");
    /* BPM text is centered on x=225, the center of the battery body. */
    br_ui.page=BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME]=0; br_ui.part=0; br_bpm=120;
    br_ui.redraw=1; br_draw_layout_old=0xffffffffu; br_draw();
    { uint16_t text[10u*26u]; char number[16];
      for(uint32_t y=0;y<10u;y++) memcpy(text+y*26u,fb+(y+13u)*240u+214u,26u*sizeof(uint16_t));
      cv_begin(240u,24u,br_track_background(0)); br_number(number,120);
      br_widget_text(225-br_widget_text_width(number)/2,13,number,C_WHITE); cv_blit(0u,0u);
      for(uint32_t y=0;y<10u;y++) HD_CHECK(!memcmp(text+y*26u,fb+(y+13u)*240u+214u,26u*sizeof(uint16_t)),"tempo centered under battery"); }
    return hd_finish("Track patches, session migration, HOME navigation and header graphics");
}
