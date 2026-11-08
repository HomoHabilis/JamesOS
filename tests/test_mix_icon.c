/* The two MIX branches must mirror and brighten in the requested direction. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
int main(void)
{
    uint16_t low[60*60];
    for(uint32_t n=0;n<2;n++) {
        int x=n?16:0,y=n?16:0;uint32_t size=n?28:60;
        int left=(x+(int)size/6)&~1,cy=(y+(int)size/2)&~1;
        int amplitude=(int)size/12;
        int top0=cy-2*amplitude;
        int top127=cy-2*(bw_mix_wave(0,1,127)*amplitude/16384);
        cv_begin(60,60,C_BLACK);bw_mix(x,y,size,0,0);memcpy(low,cv_px,sizeof low);
        assert(low[top0*60+left]==swap16(bw_tone(BR_ACC,90u)));
        for(int py=2;py<60;py++)for(int px=0;px<60;px++)
            assert((low[py*60+px]!=swap16(C_BLACK)) ==
                   (low[(2*cy+1-py)*60+px]!=swap16(C_BLACK)));
        cv_begin(60,60,C_BLACK);bw_mix(x,y,size,127,0);
        assert(cv_px[top127*60+left]==swap16(bw_tone(BR_ACC,255u)));
        int shape_changed=0;
        for(int py=2;py<60;py++)for(int px=0;px<60;px++) {
            int lit=cv_px[py*60+px]!=swap16(C_BLACK);
            assert(lit==(cv_px[(2*cy+1-py)*60+px]!=swap16(C_BLACK)));
            if(lit!=(low[py*60+px]!=swap16(C_BLACK)))shape_changed=1;
        }
        assert(shape_changed); /* The strands move with the knob. */
        memcpy(low,cv_px,sizeof low);
        cv_begin(60,60,C_BLACK);bw_mix(x,y,size,127,5);
        assert(memcmp(low,cv_px,sizeof low)); /* Glimmer changes during playback. */
        cv_begin(60,60,C_BLACK);bw_mix(x,y,size,126,5);
        assert(memcmp(low,cv_px,sizeof low)); /* Turning the knob moves the dust. */
    }
    br_ui.part=0;br_quiet_part[0]=1;br_playing=0;fm1_ms=1000;
    assert(br_widget_phase(BR_WIDGET_MIX)==0u);
    fm1_ms+=125;assert(br_widget_phase(BR_WIDGET_MIX)==0u);
    br_playing=1;uint32_t a=br_widget_phase(BR_WIDGET_MIX);
    fm1_ms+=125;assert(br_widget_phase(BR_WIDGET_MIX)!=a);
    puts("PASS: mix strands respond to the knob and glimmer moves during playback");
}
