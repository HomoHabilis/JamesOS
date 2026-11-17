/* The HOME status must report the model's actual adaptive voice capacity. */
#define POLY_UI_HOST_TEST 1
#include "poly_ui_host.h"
#include <assert.h>
#include <stdio.h>
static void tick(void)
{
    int32_t block[2u * HALF_FRAMES] = {0};
    ++fm1_ms; br_app_tick(); app_audio(block, HALF_FRAMES);
}
static void ticks(unsigned n) { while (n--) tick(); }
static void load_model(unsigned model) { br_load_preset(0, BR_FIRST_PLAITS + model); ticks(3); }
static int wait_poly(void) { for (unsigned n = 0; n < 8000 && !br_poly_mode_active(); ++n) tick(); return br_poly_mode_active(); }

int main(void)
{
    br_app_init(); fm1_ms = 2000;
    br_part[0].sound.voice_mode = 2u;
    tick();
    load_model(8u);
    assert(wait_poly());
    assert(!strcmp(br_oct_hint(), "POLY: 5 NOTES"));
    load_model(2u);
    ticks(8000u);
    assert(wait_poly());
    assert(!strcmp(br_oct_hint(), "POLY: 8 NOTES"));
    puts("SMK adaptive voice hint passed");
    return 0;
}
