/* Punch-in key state machine: groups, sliding, one effect at a time. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/instrument_punch.h"

#define DOWN(k) assert(br_punch_key(&s, (k), 1u))
#define UP(k) assert(br_punch_key(&s, (k), 0u))

int main(void)
{
    br_punch_keys_t s;
    memset(&s, 0, sizeof s);
    /* Group sizes 3, 2, 3, 2 and a rightmost black shift key. */
    {
        uint32_t count[5] = {0}, black = 0;
        for (uint32_t k = 0u; k < 27u; ++k) count[br_punch_group(k)]++;
        assert(count[1] == 3u && count[2] == 2u && count[3] == 3u && count[4] == 2u);
        (void)black;
    }
    /* Without shift every key is an ordinary note. */
    assert(!br_punch_key(&s, 1u, 1u) && !br_punch_key(&s, 1u, 0u));
    assert(s.active == BR_PUNCH_NONE);

    /* Shift then a group key starts that effect; release of the last key ends it. */
    DOWN(25u); assert(s.shift && s.active == BR_PUNCH_NONE);
    DOWN(8u); assert(s.active == BR_PUNCH_ECHO);
    UP(8u); assert(s.active == BR_PUNCH_NONE);

    /* Sliding within a group keeps one continuous effect: it never drops to NONE. */
    DOWN(13u); assert(s.active == BR_PUNCH_FILTER);
    DOWN(15u); assert(s.active == BR_PUNCH_FILTER);   /* overlap while sliding */
    UP(13u);   assert(s.active == BR_PUNCH_FILTER);
    DOWN(17u); UP(15u); assert(s.active == BR_PUNCH_FILTER);
    UP(17u);   assert(s.active == BR_PUNCH_NONE);

    /* One effect at a time: other groups are ignored while one runs. */
    DOWN(1u); assert(s.active == BR_PUNCH_LOOP);
    DOWN(20u); assert(s.active == BR_PUNCH_LOOP && !(s.held & (1u << 20)));
    UP(20u);   assert(s.active == BR_PUNCH_LOOP);     /* its release changes nothing */
    DOWN(22u); UP(22u);
    UP(1u);    assert(s.active == BR_PUNCH_NONE);
    /* After the first ends, the next press can start another group. */
    DOWN(20u); assert(s.active == BR_PUNCH_TREM); UP(20u);

    /* Releasing shift ends a running effect; late group-key releases are still
     * consumed, so they do not become note-offs. */
    DOWN(3u); assert(s.active == BR_PUNCH_LOOP);
    UP(25u);   assert(!s.shift && s.active == BR_PUNCH_NONE && !s.held);
    UP(3u);    assert(!s.consumed);
    /* A group key pressed before shift is a normal note, even if shift follows. */
    assert(!br_punch_key(&s, 10u, 1u));
    DOWN(25u);
    assert(!br_punch_key(&s, 10u, 0u));                // its note-off is not ours
    UP(25u);
    /* Non-group keys (white keys, key 0) are never consumed, shift or not. */
    DOWN(25u);
    for (uint32_t k = 0u; k < 25u; ++k) if (!br_punch_group(k)) assert(!br_punch_key(&s, k, 1u));
    UP(25u);
    /* Settings round trip: defaults save as zeros, every other setting survives. */
    {
        br_punch_cfg_t c = BR_PUNCH_CFG_DEFAULTS, back;
        uint8_t bytes[4];
        br_punch_cfg_pack(&c, bytes);
        assert(!bytes[0] && !bytes[1] && !bytes[2] && !bytes[3] && br_punch_cfg_packed_valid(bytes));
        for (uint32_t trial = 0u; trial < 5000u; ++trial) {
            uint32_t r = trial * 2654435761u;
            c.loop_len = r & 3u; c.echo_time = (r >> 2) & 3u; c.filter_shape = (r >> 4) & 3u;
            c.trem_rate = (r >> 6) & 3u; c.echo_fb = (r >> 8) & 31u;
            c.filter_amount = (r >> 13) & 31u; c.trem_depth = (r >> 18) & 31u;
            br_punch_cfg_pack(&c, bytes);
            assert(br_punch_cfg_packed_valid(bytes));
            br_punch_cfg_unpack(&back, bytes);
            assert(!memcmp(&c, &back, sizeof c));
        }
        bytes[0] = 0x99u; assert(!br_punch_cfg_packed_valid(bytes));
        bytes[0] = BR_PUNCH_SAVE_MARK; bytes[3] = 0x80u; assert(!br_punch_cfg_packed_valid(bytes));
    }
    /* Effects: trem chops at the tempo grid and filter changes the signal; others do nothing. */
    {
        br_punch_cfg_t c = BR_PUNCH_CFG_DEFAULTS;
        br_punch_dsp_t d; int32_t buf[512], ref[512];
        const uint32_t quarter = 22050u;
        memset(&d, 0, sizeof d);
        for (uint32_t i = 0u; i < 512u; ++i) ref[i] = 10000;
        memcpy(buf, ref, sizeof buf);
        br_punch_process(&d, &c, BR_PUNCH_ECHO, buf, 256u, quarter);   /* no line: untouched */
        assert(!memcmp(buf, ref, sizeof buf));
        br_punch_process(&d, &c, BR_PUNCH_NONE, buf, 256u, quarter);
        assert(!memcmp(buf, ref, sizeof buf));
        c.trem_rate = 0u; c.trem_depth = 31u;               /* 1/4 notes, full depth */
        memset(&d, 0, sizeof d);
        uint32_t high = 0u, low = 0u;
        for (uint32_t block = 0u; block < 4u * quarter / 256u; ++block) {
            for (uint32_t i = 0u; i < 512u; ++i) buf[i] = 10000;
            br_punch_process(&d, &c, BR_PUNCH_TREM, buf, 256u, quarter);
            if (buf[0] > 9000) ++high; if (buf[0] < 1000) ++low;
        }
        assert(high > 40u && low > 40u && high + low > 150u);     /* chopped, half on, half off */
        c.filter_shape = 0u; c.filter_amount = 31u; memset(&d, 0, sizeof d);
        for (uint32_t block = 0u; block < 40u; ++block) {
            for (uint32_t i = 0u; i < 512u; ++i) buf[i] = (i / 2u) & 1u ? 10000 : -10000;
            br_punch_process(&d, &c, BR_PUNCH_FILTER, buf, 256u, quarter);
        }
        assert(buf[300] > -3000 && buf[300] < 3000);               /* LP removes the fast square */
    }
    /* Echo: an impulse comes back after one note value, quieter, and repeats. */
    {
        static int16_t line[8192];
        br_punch_cfg_t c = BR_PUNCH_CFG_DEFAULTS;
        br_punch_dsp_t d; int32_t buf[512];
        const uint32_t quarter = 22050u, delay_frames = 11025u;      /* 1/8 at 120 BPM */
        uint32_t first = 0u, second = 0u, frame = 0u;
        int32_t peak_first = 0;
        memset(&d, 0, sizeof d); d.line = line; d.line_len = 8192u;
        c.echo_time = 2u; c.echo_fb = 31u;
        for (uint32_t block = 0u; block < 200u; ++block) {
            for (uint32_t i = 0u; i < 512u; ++i) buf[i] = 0;
            if (block == 0u) buf[0] = buf[1] = 20000;
            br_punch_process(&d, &c, BR_PUNCH_ECHO, buf, 256u, quarter);
            for (uint32_t i = 0u; i < 256u; ++i, ++frame) {
                int32_t v = buf[2u * i] > 0 ? buf[2u * i] : -buf[2u * i];
                if (frame > 2000u && v > 1500) {
                    if (!first) first = frame;
                    else if (frame > first + 1000u && !second) second = frame;
                }
                if (frame > 2000u && v > peak_first && !second) peak_first = v;
            }
        }
        assert(first > delay_frames - 200u && first < delay_frames + 200u);
        assert(second > 2u * delay_frames - 400u && second < 2u * delay_frames + 400u);
        assert(peak_first < 12000);                          /* quieter than the input */
        /* Dotted 1/8 is three sixteenths of a bar-beat: 3/4 of a quarter note. */
        memset(line, 0, sizeof line); memset(&d, 0, sizeof d); d.line = line; d.line_len = 8192u;
        c.echo_time = 3u; first = 0u; frame = 0u;
        for (uint32_t block = 0u; block < 100u; ++block) {
            for (uint32_t i = 0u; i < 512u; ++i) buf[i] = 0;
            if (block == 0u) buf[0] = buf[1] = 20000;
            br_punch_process(&d, &c, BR_PUNCH_ECHO, buf, 256u, quarter);
            for (uint32_t i = 0u; i < 256u; ++i, ++frame)
                if (!first && frame > 2000u && buf[2u * i] > 1500) first = frame;
        }
        assert(first > 16537u - 200u && first < 16537u + 200u);
        /* Leaving echo clears the borrowed line. */
        br_punch_process(&d, &c, BR_PUNCH_NONE, buf, 256u, quarter);
        for (uint32_t i = 0u; i < 8192u; ++i) assert(!line[i]);
    }
    printf("punch key state machine, settings and effect checks passed\n");
    return 0;
}
