/* Mixer signal meters: the peak of each track's last block, mapped to dB-spaced segments. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

int main(void)
{
    int32_t a[BR_BLOCK * 2u];
    uint32_t left, right, peak0 = 0, i;
    br_app_init(); fm1_ms = 1000u; br_fx_meter_enable(1u);
    /* silence: the meter is empty */
    for (int b = 0; b < 8; ++b) { memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
    br_meter_peak(0u, &left, &right);
    assert(left == 0u && right == 0u);
    /* a note on track 1 at full level fills its meter and leaves the others empty */
    br_part[0].revision++; br_part[0].sound.level = 127u; RING_PUBLISH(); br_part[0].revision++;
    br_engine_note_on(0u, 60u, 127u);
    for (int b = 0; b < 40; ++b) {
        memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        br_meter_peak(0u, &left, &right); if (left > peak0) peak0 = left;
    }
    { int32_t mx = 0; for (int j = 0; j < (int)(BR_BLOCK * 2u); ++j) if (abs(a[j]) > mx) mx = abs(a[j]);
      printf("BR_BLOCK=%u output max %d, track 1 raw peak %u\n", (unsigned)BR_BLOCK, (int)mx, (unsigned)peak0); }
    assert(peak0 > 100u);
    br_meter_peak(1u, &left, &right); assert(left == 0u && right == 0u);
    /* The meter follows the rendered track FX, including overdrive. */
    br_fx_meter_value[0] = 0u;
    br_part[0].revision++;
    br_part[0].sound.drive = 127u;
    br_part[0].fx.drive_mode = 2u;
    br_part[0].fx.drive_gain = 110u;
    br_part[0].fx.drive_tone = 100u;
    RING_PUBLISH(); br_part[0].revision++;
    uint32_t wet_peak = 0u, differs_from_dry = 0u;
    for (int b = 0; b < 40; ++b) {
        br_fx_meter_value[0] = 0u;
        memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        uint32_t post = br_fx_meter_read(0u), dry_peak = 0u, expected = 0u;
        for (uint32_t j = 0; j < BR_BLOCK; ++j) {
            int32_t v = bus[0][j]; uint32_t mag = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
            if (mag > dry_peak) dry_peak = mag;
            for (uint32_t ch = 0u; ch < 2u; ++ch) {
                v = br_fx_track_stereo[0][j][ch]; mag = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
                if (mag > expected) expected = mag;
            }
        }
        if (expected > 32768u) expected = 32768u;
        assert(post == expected);
        if (post > wet_peak) wet_peak = post;
        if (post != dry_peak) differs_from_dry = 1u;
    }
    assert(wet_peak > 0u && differs_from_dry);
    /* the segment mapping is monotonic and spans -60..0 dBFS */
    uint32_t last = 0;
    for (i = 0; i <= 32768u; i += 64u) { uint32_t s = br_meter_segments(i, 28u); assert(s >= last && s <= 28u); last = s; }
    assert(br_meter_segments(0u, 28u) == 0u && br_meter_segments(32768u, 28u) == 28u);
    assert(br_meter_segments(33u, 28u) == 0u);          /* below -60 dBFS */
    printf("mixer meter: ok\n");
    return 0;
}
