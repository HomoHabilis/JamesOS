/* Exercise both the stable and transitioning master heat paths on the host. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static int32_t shape(uint32_t type, uint32_t amount, int32_t sample)
{
    br_master.fx.heat_type=type;br_master.fx.heat_amount=amount;
    br_master_fx_init();
    assert(br_master_fx_begin_block(32));
    assert(br_master_state.saturation==amount);
    assert(br_master_state.wow==(type==BR_HEAT_WOW?amount:0u));
    return br_master_shape(sample,br_master_state.saturation
#if BR_FX_MASTER_COEFF_CACHE
                           ,br_master_state.stable_saturation_gain
#endif
    );
}
int main(void)
{
    br_app_init();
    for(uint32_t amount=16;amount<=112;amount+=48) {
        int32_t sat=shape(BR_HEAT_SAT,amount,12000);
        int32_t wow=shape(BR_HEAT_WOW,amount,12000);
        assert(sat==wow && sat!=12000);
    }
    br_master.fx.heat_type=BR_HEAT_WOW;br_master.fx.heat_amount=96;
    br_master_fx_init();
    int32_t audio[512];for(unsigned i=0;i<512;i++)audio[i]=(i&2)?12000:-12000;
    br_master_fx_process(audio,256);
    assert(br_master_state.wow_valid_samples>0 && br_master_state.saturation==96);
    br_master.fx.heat_type=BR_HEAT_BIT;br_master.fx.heat_amount=64;
    for(unsigned i=0;i<4;i++)br_master_fx_process(audio,256);
    assert(br_master_state.heat_type==BR_HEAT_BIT && !br_master_state.saturation && !br_master_state.wow);
    br_master.fx.heat_type=BR_HEAT_WOW;br_master.fx.heat_amount=64;
    for(unsigned i=0;i<4;i++)br_master_fx_process(audio,256);
    assert(br_master_state.heat_type==BR_HEAT_WOW && br_master_state.saturation==64 && br_master_state.wow==64);
    puts("PASS: WOW saturation, pitch processing, and heat transitions");
}
