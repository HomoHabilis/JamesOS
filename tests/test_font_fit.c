/* Host-only text-fit audit. The runner instruments a temporary copy of the
 * widget renderer; it never modifies firmware or accesses the device. */
#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void draw_page(uint32_t part, uint32_t page, uint32_t screen)
{
    br_ui.part=part; br_ui.page=page; br_ui.screen[page]=screen;
    br_draw_layout_old=0xffffffffu; br_ui.redraw=1u; br_draw();
}

int main(void)
{
    static const uint32_t synth_presets[]={0u, 8u, BR_FIRST_PLAITS, BR_PRESET_COUNT-1u};
    static const uint32_t kits[]={BR_KIT_BRAIDS, BR_KIT_PLAITS, BR_KIT_808, BR_KIT_909};
    uint32_t screens=0, variations=0;
    br_app_init(); cv_begin(60,40,BR_BACKGROUND); font_audit_silent=1;
    font_audit(58,0,"TEST",1); assert(font_audit_count==1);
    font_audit(0,0,"TEST",1); assert(font_audit_count==1);
    font_param_prev=-1; font_audit_param_row(30,76,"LONG"); font_audit_param_row(30,83,"TEXT");
    assert(font_collision_count==1);
    font_audit_badge(100,107,1); assert(font_collision_count==2);
    font_audit_silent=0; font_audit_count=0;
    font_collision_count=0;
    cv_begin(60,134,BR_BACKGROUND);
    br_draw_param_text(30,76,"AAAAAAAAAA","WIDE LABEL",C_WHITE);
    assert(font_audit_count==0 && font_collision_count==0);
    /* Drum-ruler numbers must fit each 12px step cell for all 64 steps. */
    for(int n=1;n<=64;n++) {
      char number[16]; br_number(number,n);
      assert(br_widget_text_width(number)<=12);
    }
    /* Header tempo stays right of the metronome and on screen, including
     * the maximum older saved tempo accepted by the session reader. */
    for(int n=BR_BPM_MIN;n<=BR_BPM_STORED_MAX;n++) {
      char number[16]; br_number(number,n);
      int width=br_widget_text_width(number), x=223-width/2;
      assert(x>208 && x+width<=240);
    }
    for(uint32_t part=0;part<BR_PARTS;part++)
      for(uint32_t preset=0;preset<4; preset++)
        for(uint32_t page=0;page<BR_PAGE_COUNT;page++)
          for(uint32_t screen=0;screen<8;screen++) {
            br_app_init(); br_ui.part=part; br_ui.page=page; br_ui.screen[page]=screen;
            br_part[part].sound.preset=part==BR_DRUM_PART?kits[preset]:synth_presets[preset];
            if(screen>=br_screen_count())continue;
            draw_page(part,page,screen); screens++;
            /* Each control is exercised at each extreme, independently. */
            for(uint32_t k=0;k<4;k++) for(int direction=-1;direction<=1;direction+=2) {
                br_control_turn(k,direction*10000);
                draw_page(part,page,screen); variations++;
                br_control_turn(k,-direction*10000);
            }
          }
    /* Exercise every permitted engine/kit title on every track. */
    for(uint32_t part=0;part<BR_PARTS;part++) for(uint32_t preset=0;preset<BR_PRESET_COUNT;preset++) {
      if(!br_preset_allowed_on_part(part,preset)) continue;
      br_app_init(); br_part[part].sound.preset=preset;
      draw_page(part,BR_PAGE_HOME,0); variations++;
    }
    /* Scroll each sounds/kits browser through its available rows. */
    for(uint32_t part=0;part<BR_PARTS;part++) {
      br_app_init(); br_ui.part=part; br_ui.page=BR_PAGE_SCALE; br_ui.screen[BR_PAGE_SCALE]=0;
      for(uint32_t i=0;i<100;i++) { br_sel_move(1); draw_page(part,BR_PAGE_SCALE,0); variations++; }
    }
    /* Project menu, save/name/overwrite/confirmation, first and last slots. */
    for(int mode=1;mode<=6;mode++) for(int slot=0;slot<12;slot+=11) {
      br_app_init(); es.mode=mode; es.slot=slot; es.cursor=11;
      memcpy(es.name,"AAAAAAAAAAAA",12); es.name[12]=0;
      for(int i=0;i<12;i++) { es_directory[i].state=1; memcpy(es_directory[i].name,"AAAAAAAAAAAA",13); }
      es_draw(); variations++;
    }
    /* Every character allowed in a project name, repeated at maximum length.
     * The name editor's 14px cells must also fit the 2x glyph itself. */
    {
      const char *alphabet=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
      for(const char *ch=alphabet;*ch;ch++) {
        assert(2u*james_font_width((uint8_t)*ch)<=14u);
        for(int mode=3;mode<=6;mode++) {
          br_app_init(); es.mode=mode; es.slot=11; es.cursor=11;
          memset(es.name,*ch,12); es.name[12]=0;
          for(int i=0;i<12;i++) {
            es_directory[i].state=1; memset(es_directory[i].name,*ch,12);
            es_directory[i].name[12]=0;
          }
          es_draw(); variations++;
        }
      }
    }
    printf("font audit: %u screen/preset combinations, %u control/project variations\n",screens,variations);
    printf("font audit: %u distinct horizontal clipping cases\n",font_audit_count);
    printf("font audit: %u column, vertical or header collision observations\n",font_collision_count);
    assert(screens>100 && variations>800);
    return 0;
}
