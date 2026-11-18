/* Global tempo and swing must affect all tracks, including restored projects. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    br_app_init(); fm1_ms=2000u;
    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=1u;
    assert(!strcmp(br_control(0).label,"STEPS"));
    assert(!strcmp(br_control(1).label,"SPEED"));
    assert(!strcmp(br_control(2).label,"QUANTIZE") && !br_control(3).label[0]);
    uint32_t tempo=br_bpm;
    br_control_turn(3u,15);
    assert(br_bpm==tempo && br_swing==0u);
    br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u; br_ui.part=2u;
    assert(!strcmp(br_control(0).label,"BPM"));
    br_control_t bpm_control = br_control(0u);
    assert(br_control_widget(0u, &bpm_control)==BR_WIDGET_BPM);
    {
        uint32_t counts[3] = {0u,0u,0u};
        const uint32_t levels[3] = {0u,64u,127u};
        for (uint32_t i=0;i<3u;++i) {
            cv_begin(60,60,BR_BACKGROUND); cv_oy=0; bw_bpm(0,0,60,levels[i],0u);
            for (uint32_t pixel=0;pixel<cv_w*cv_h;++pixel)
                if (cv_px[pixel]==swap16(BR_ACC)) ++counts[i];
        }
        assert(counts[0] < counts[1] && counts[1] < counts[2]);
        assert(cv_px[52u*cv_w+30u] != swap16(BR_ACC)); /* bottom gap stays open */
        {
            uint16_t frame[60u*60u];
            br_playing=0; assert(br_widget_phase(BR_WIDGET_BPM)==0u);
            br_playing=1; br_transport_phase=0u;
            cv_begin(60,60,BR_BACKGROUND); bw_bpm(0,0,60,64u,br_widget_phase(BR_WIDGET_BPM));
            memcpy(frame,cv_px,sizeof frame);
            br_transport_phase=64u;
            cv_begin(60,60,BR_BACKGROUND); bw_bpm(0,0,60,64u,br_widget_phase(BR_WIDGET_BPM));
            assert(memcmp(frame,cv_px,sizeof frame));
            br_playing=0;
        }
    }
    assert(!strcmp(br_control(1).label,"SWING"));
    br_control_t swing = br_control(1u);
    assert(br_control_widget(1u, &swing)==BR_WIDGET_SWING);
    /* HD: the same intent, measured from what is drawn rather than exact pixels (the note heads and
     * beam corners are anti-aliased). (a) Accent content at rest and at maximum. (b) On the row
     * through the stems (y = s/2) there are three stems; the outer two stay put and the middle one's
     * luma-weighted centre (1/16 px) moves monotonically right, at most one 2-pixel step per q, and
     * sits within 1 px of the standard build's stem (30 + q*60/(127*8), even) at every q, so the
     * total travel (6 px at s = 60) matches too. */
    {
        int32_t centre[128], outer[2] = {0, 0};
        for (uint32_t q=0; q<=127u; ++q) {
            cv_begin(60,60,BR_BACKGROUND); cv_oy=0; bw_swing(0,0,60,q);
            if (q==0u || q==127u) {
                uint32_t accent=0u; hd_box_t box=hd_box(cv_px,(int32_t)cv_w,(int32_t)cv_h,BR_BACKGROUND,1);
                for (uint32_t pixel=0;pixel<cv_w*cv_h;++pixel) accent += hd_near(cv_px[pixel],swap16(BR_ACC),2u,1);
                HD_CHECK(accent >= 200u, "swing q=%u: %u accent pixels (want >= 200)", q, accent);
                HD_CHECK(box.left >= 0 && box.left <= 12 && box.right >= 48 && box.right < 56,
                         "swing q=%u: drawing spans x %d..%d (want about 12..49)", q, box.left, box.right);
            }
            /* Runs of non-background pixels on row 30, each with its luma-weighted centre. */
            int32_t runs=0, run_centre[8];
            uint32_t bg_luma=hd_luma(BR_BACKGROUND);
            for (uint32_t x=0; x<cv_w;) {
                uint16_t c=swap16(cv_px[30u*cv_w+x]);
                if (c==BR_BACKGROUND) { ++x; continue; }
                int64_t sum=0, weight=0;
                for (; x<cv_w && swap16(cv_px[30u*cv_w+x])!=BR_BACKGROUND; ++x) {
                    uint32_t l=hd_luma(swap16(cv_px[30u*cv_w+x]));
                    int64_t w=l>bg_luma ? l-bg_luma : bg_luma-l;
                    sum += w*(int64_t)(x*16u+8u); weight += w;
                }
                if (runs<8) run_centre[runs]=weight ? (int32_t)(sum/weight) : -1;
                ++runs;
            }
            HD_CHECK(runs==3, "swing q=%u: %d stems on the stem row (want 3)", q, runs);
            if (runs!=3) { centre[q]=q ? centre[q-1u] : 0; continue; }
            centre[q]=run_centre[1];
            if (!q) { outer[0]=run_centre[0]; outer[1]=run_centre[2]; }
            HD_CHECK(run_centre[0]==outer[0] && run_centre[2]==outer[1],
                     "swing q=%u: outer stems moved (%d,%d vs %d,%d /16 px)", q,
                     run_centre[0], run_centre[2], outer[0], outer[1]);
            int32_t want=(int32_t)(((30u+q*60u/(127u*8u))&~1u)*16u+8u);
            HD_CHECK(centre[q]>=want-16 && centre[q]<=want+16,
                     "swing q=%u: middle stem centre %d/16 px, want %d/16 +- 16", q, centre[q], want);
            if (q) HD_CHECK(centre[q]>=centre[q-1u] && centre[q]-centre[q-1u]<=32,
                            "swing q=%u: middle stem moved from %d to %d /16 px", q, centre[q-1u], centre[q]);
        }
        int32_t travel=centre[127]-centre[0];
        HD_CHECK(travel>=5*16 && travel<=7*16, "swing: middle stem travels %d/16 px (want 6 px +- 1)", travel);
        assert(hd_failures==0u);
    }
    br_control_turn(0u,7); br_control_turn(1u,24);
    assert(br_bpm==tempo+7u && br_swing==24u);
    for (uint32_t p=0;p<BR_PARTS;++p) {
        br_ui.part=p;
        assert(br_control(1).value==24);
        br_part[p].division=1u;
        assert(br_step_duration(p,0)==br_step_duration(0,0));
        assert(br_step_duration(p,1)==br_step_duration(0,1));
    }
    assert(br_step_duration(0,0)>br_step_duration(0,1));
    br_session_t saved; assert(br_capture(&saved));
    br_swing=0u; br_bpm=tempo;
    assert(br_restore(&saved));
    assert(br_swing==24u && br_bpm==tempo+7u);
    for (uint32_t p=0;p<BR_PARTS;++p) {
        assert(br_step_duration(p,0)==br_step_duration(0,0));
        assert(br_step_duration(p,1)==br_step_duration(0,1));
    }
    /* The global MIX swing control changes the shared setting. */
    br_ui.part=3u; br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u;
    assert(br_control(1).value==24); br_control_turn(1,1);
    assert(br_swing==25u && br_control(1).value==25);
    swing=br_control(1u); assert(br_control_widget(1u,&swing)==BR_WIDGET_SWING);
    {
        char text[24];
        br_swing=0u; swing=br_control(1u); br_control_text(1u,&swing,text);
        assert(!strcmp(text,"50%") && swing.high==BR_SWING_MAX);
        br_control_turn(1u,1000); swing=br_control(1u); br_control_text(1u,&swing,text);
        assert(!strcmp(text,"80%") && br_swing==BR_SWING_MAX);
        assert(br_step_duration_from_base(0,0,12800u)==20500u);
        assert(br_step_duration_from_base(0,1,12800u)==5100u);
        assert(br_capture(&saved) && br_session_valid(&saved));
        br_swing=0; assert(br_restore(&saved) && br_swing==BR_SWING_MAX);
        br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u;
        swing=br_control(1u); br_control_text(1u,&swing,text);
        assert(!strcmp(text,"80%") && swing.high==BR_SWING_MAX);
    }
    {
        const uint32_t tempos[] = {60u, 120u, 200u, 240u};
        br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u;
        bpm_control=br_control(0u);
        assert(bpm_control.low==60 && bpm_control.high==240);
        for (uint32_t i=0;i<sizeof tempos/sizeof tempos[0];++i) {
            br_bpm=tempos[i];
            assert(br_capture(&saved) && br_session_valid(&saved));
            br_bpm=120u; assert(br_restore(&saved) && br_bpm==tempos[i]);
            assert(br_tempo_quarter_frames()==FS*60u/tempos[i]);
            assert(br_fx_delay_note_samples(br_fx_delay_note_store(8u),tempos[i])==
                   (FS*60u+tempos[i]/2u)/tempos[i]);
        }
        /* Out-of-range tempos are clamped on restore (v17 has no stored range beyond 60..240). */
        saved.b[br_v17_wire_at(BR_V17_ROW(G, bpm), 0u, 0u)]=250u;
        assert(br_session_valid(&saved) && br_restore(&saved) && br_bpm==240u);
        br_bpm=120u; assert(br_capture(&saved)); saved.b[br_v17_wire_at(BR_V17_ROW(G, bpm), 0u, 0u)]=40u;
        assert(br_session_valid(&saved) && br_restore(&saved) && br_bpm==60u);
    }
    return 0;
}
