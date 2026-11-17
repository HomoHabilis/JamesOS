/* Host-only successful import/load path for one converted historical project.
 * No interrupted writes and no device access. */
#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#include "../tools/legacy_lfo_rate_map.h"
#include "../tools/legacy_delay_map.h"
#include <stdio.h>
static uint8_t upload[PPS_MAX_PAYLOAD];
static int import_may_erase(void *ctx, uint32_t off)
{
    (void)ctx;
    return pps_owner_may_erase(&runtime.owner[0], off) || pps_owner_may_erase(&runtime.owner[1], off);
}
int main(int argc, char **argv)
{
    fm1_fixed_runtime_port port = {0};
    char name[PS_NAME_BYTES];
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    size_t len = fread(upload, 1, sizeof upload, f);
    assert(fgetc(f) == EOF && !ferror(f) && len); fclose(f);
    port.begin=port_begin; port.set_mode=port_mode; port.end=port_end;
    port.fence=port_fence; port.faulted=port_faulted; port.pair_state=port_state;
    port.certificate=port_cert; port.activate=port_activate; port.commit_slot=port_slot;
    port.role[0]=1; port.role[1]=2; port.role[2]=3; port.role[3]=4;
    port.named_slots=12; port.fixed_partition_auto=1; mutable_port=&port;
    init_runtime_fixture(); parkops.may_erase=import_may_erase;
    assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
    assert(fm1_fixed_runtime_import(&runtime,1,"MIGRATED",upload,len)==PFS_OK);
    assert(fm1_fixed_runtime_scan(&runtime,1,name)==PFS_OK && !strcmp(name,"MIGRATED"));
    br_bpm=61u;
    assert(fm1_fixed_bridge_load(&runtime.bridge,1)==PFS_OK);
    assert(br_bpm==120u && br_ps_state.used==0x3fu && br_ps_state.active==0u);
    {
        static const unsigned numerator[11] = {1,1,1,1,1,1,1,2,2,3,4};
        static const unsigned denominator[11] = {8,6,4,3,2,1,1,1,1,1,1};
        for (unsigned value=0; value<128u; ++value) {
            unsigned index=value*11u/128u, div=denominator[index]*120u;
            unsigned old_samples=(FS*60u*numerator[index] + div/2u)/div;
            unsigned new_samples=br_fx_delay_note_samples(jamesos_v16_delay_time(value),120u);
            assert(old_samples > new_samples ? old_samples-new_samples<=1u : new_samples-old_samples<=1u);
            assert(jamesos_current_lfo_hz_q20(value)==br_lfo_rate_hz_q20(value<<8));
        }
    }
    for (unsigned p=0; p<BR_PARTS; ++p) {
        const volatile br_route_config_t *a=&br_part[p].mod.route[BR_ROUTES];
        const volatile br_route_config_t *b=&br_part[p].mod.route[BR_ROUTES+1u];
        const volatile br_route_config_t *hidden=&br_part[p].mod.route[2u];
        assert(a->source==5u && a->target==2u && a->amount==-20 && a->enabled);
        assert(b->source==4u && b->target==4u && b->amount==24 && !b->enabled);
        if (p != 0u)
            assert(!br_part[p].mod.route[0].enabled && !br_part[p].mod.route[1].enabled);
        assert(hidden->source==8u && hidden->target==1u && hidden->amount==41 && hidden->enabled);
        assert(br_part[p].mod.lfo[0].target==1u && br_part[p].mod.lfo[0].depth==41u);
    }
    assert(br_part[0].sound.timbre_mod==64u && br_part[0].sound.fm_mod==64u &&
           br_part[0].sound.morph_mod==64u);
    assert(br_part[0].mod.route[0].source==5u && br_part[0].mod.route[0].target==2u &&
           br_part[0].mod.route[0].amount==64 && br_part[0].mod.route[0].enabled);
    assert(br_part[0].mod.route[1].source==5u && br_part[0].mod.route[1].target==1u &&
           br_part[0].mod.route[1].amount==-89 && br_part[0].mod.route[1].enabled);
    assert(br_part[0].mod.route[3].source==5u && br_part[0].mod.route[3].target==3u &&
           br_part[0].mod.route[3].amount==127 && br_part[0].mod.route[3].enabled);
    {
        br_mod_config_t config;
        br_sound_t base, sound;
        br_fx_config_t fx, fx_out;
        br_mod_state_t state;
        int32_t pitch;
        memcpy(&config, (const void *)&br_part[0].mod, sizeof config);
        memcpy(&base, (const void *)&br_part[0].sound, sizeof base);
        memcpy(&fx, (const void *)&br_part[0].fx, sizeof fx);
        base.preset=BR_FIRST_PLAITS; base.timbre=40u; base.color=0u;
        base.filter_env_depth=0; config.target=BR_TARGET_OFF;
        config.lfo[0].wave=BR_WAVE_SQUARE;
        br_mod_init(&state, 1u);
        state.phase[0]=0u; state.note=0u; state.env=0u;
        br_mod_effective(&state,&config,&base,&fx,0,&sound,&fx_out,&pitch);
        /* 1.0a: HOME timbre +64 plus PATCH 1 -20 = +44;
         * HOME morph +127. Pitch has bounded quantization from the new law. */
        assert(sound.timbre==84u && sound.color==127u);
        assert(pitch < -1160 && pitch > -1250);
        state.phase[0]=0x80000000u;
        br_mod_effective(&state,&config,&base,&fx,0,&sound,&fx_out,&pitch);
        assert(sound.timbre==0u && sound.color==0u);
        assert(pitch > 1160 && pitch < 1250);
    }
    {
        static br_session_t session;
        assert(br_capture(&session) && session.b[4]==BR_V17_VERSION);
    }
    puts("converted v4 project: actual app import, load and current session capture passed");
    return 0;
}
