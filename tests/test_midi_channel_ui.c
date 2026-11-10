/* MIDI controls draw the actual channel beside a five-pin plug. */
#define DINGHY_HOST 1
#define MIDI_OUT_TEST_CAPTURE 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    br_app_init(); fm1_ms=5000u; br_ui.part=0u; br_ui.page=BR_PAGE_MIX;
    for (uint32_t i=0; i<7u; ++i) {
        uint32_t page=i<3u?7u:8u, knob=i<3u?i:i-3u;
        br_ui.screen[BR_PAGE_MIX]=page;
        for (uint32_t channel=0; channel<=16u; ++channel) {
            br_midi_channels[i]=(uint8_t)channel;
            br_control_t c=br_control(knob); char text[24];
            assert(c.low==0 && c.high==16 && c.value==(int32_t)channel);
            assert(br_control_widget(knob,&c)==BR_WIDGET_MIDI_CHANNEL);
            assert(br_control_widget_variant(knob,&c,BR_WIDGET_MIDI_CHANNEL)==channel);
            br_control_text(knob,&c,text);
            if (!channel) assert(!strcmp(text,"OFF"));
            else { char expected[4]; snprintf(expected,sizeof expected,"%u",channel); assert(!strcmp(text,expected)); }
        }
        br_midi_channels[i]=1u; br_control_turn(knob,-1); assert(br_midi_channels[i]==0u);
        br_control_turn(knob,1); assert(br_midi_channels[i]==1u);
    }
    br_ui.screen[BR_PAGE_MIX]=7u;
    { br_control_t c=br_control(3u); assert(br_control_widget(3u,&c)==BR_WIDGET_CHOICE); }
    /* OFF suppresses new output and releases an already-sent note once. */
    br_app_init(); br_ui.page=BR_PAGE_HOME; br_ui.part=0u; br_arp_on=0u; br_hold_on=0u;
    br_midi_channels[0]=0u; host_midi_out_count=0u;
    br_key_down(0u,1); br_key_up(0u); assert(!host_midi_out_count);
    br_midi_channel_set(0u,1u); br_key_down(0u,1);
    assert(host_midi_out_count==1u && host_midi_out_last_status==0x90u);
    br_midi_channel_set(0u,0u);
    assert(host_midi_out_count==2u && host_midi_out_last_status==0x80u);
    br_key_up(0u); assert(host_midi_out_count==2u);
    /* Each drum lane has its own OFF state and sends on its own channel. */
    br_ui.part=BR_DRUM_PART; br_midi_channels[3]=0u; br_midi_channels[4]=5u;
    host_midi_out_count=0u; br_key_down(0u,1); br_key_up(0u); assert(!host_midi_out_count);
    br_key_down(1u,1); assert(host_midi_out_count==1u && host_midi_out_last_status==0x94u);
    br_key_up(1u); assert(host_midi_out_count==2u && host_midi_out_last_status==0x84u);
    /* OFF also ends a held/sustained outgoing note without a duplicate later. */
    br_ui.part=0u; br_midi_channel_set(0u,1u); br_hold_on=1u;host_midi_out_count=0u;
    br_key_down(0u,1);br_key_up(0u);assert(host_midi_out_count==1u);
    br_midi_channel_set(0u,0u);assert(host_midi_out_count==2u && host_midi_out_last_status==0x80u);
    br_hold_release_all();assert(host_midi_out_count==2u);br_hold_on=0u;
    { uint16_t prior[60u*60u];
      for(uint32_t channel=0; channel<=16u; ++channel){
        cv_begin(60u,60u,BR_BACKGROUND);cv_oy=0;bw_midi_channel(0,0,60u,channel);
        /* The plug is mirrored about the shifted center between pixels 15/16. */
        for (uint32_t y=0; y<60u; ++y)
            for (uint32_t x=0u; x<16u; ++x)
                assert(cv_px[y*60u+x]==cv_px[y*60u+31u-x]);
        if(channel)assert(memcmp(prior,cv_px,sizeof prior));
        memcpy(prior,cv_px,sizeof prior);
      }
    }
    if(argc>1){br_ui.page=BR_PAGE_MIX;br_ui.screen[BR_PAGE_MIX]=7u;br_midi_channels[0]=0u;br_midi_channels[1]=1u;br_midi_channels[2]=16u;br_ui.redraw=1u;br_draw();write_ppm_as(argv[1]);}
    puts("MIDI channel controls, OFF routing values and 0–16 icons passed");
    return 0;
}
