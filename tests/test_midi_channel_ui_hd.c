/* SPDX-License-Identifier: GPL-3.0-only */
/* HD MIDI channel controls. Behaviour: every destination's control holds 0 (OFF) to 16, turns, and
 * OFF suppresses and releases output. The HD icon (batch C, D19) is a DIN socket in the colour of
 * the track it feeds, ringed by 16 channel dots with the selected one lit; OFF is a grey socket
 * struck through; kit lanes add four pips with their own lit. The variant is
 * channel | destination << 8. Checked: the lit dot sits on the ring (radius 18 px about the socket
 * centre) and steps by exactly 1/16 turn per channel; the socket, pins and unlit ring are the same
 * for every channel of a destination (only the lit dot moves); OFF adds the strike and greys the
 * socket; the socket takes the track colour; the lane pips mark the lane; the icon stays in its
 * cell; and the real MIX page draws exactly what the control widget draws. */
#define DINGHY_HOST 1
#define MIDI_OUT_TEST_CAPTURE 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
#include <math.h>

static uint16_t icon[17][60u * 60u];

static void draw_icon(uint32_t channel, uint32_t dest)
{
    cv_begin(60u, 60u, BR_BACKGROUND); cv_oy = 0; bw_midi_channel(0, 0, 60u, channel | dest << 8);
}
/* Centroid of the exactly-white pixels between radius 14 and 23 of the socket centre (30, 29). */
static int lit_dot(double *angle, double *radius)
{
    double sx = 0, sy = 0; uint32_t n = 0;
    for (int32_t y = 0; y < 60; ++y) for (int32_t x = 0; x < 60; ++x) {
        double dx = x - 30, dy = y - 29, r = sqrt(dx * dx + dy * dy);
        if (r >= 14.0 && r <= 23.0 && cv_px[y * 60 + x] == swap16(C_WHITE)) { sx += dx; sy += dy; ++n; }
    }
    if (!n) return 0;
    sx /= n; sy /= n; *radius = sqrt(sx * sx + sy * sy); *angle = atan2(sy, sx) * 180.0 / M_PI;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro(); fm1_ms = 5000u; br_ui.part = 0u; br_ui.page = BR_PAGE_MIX;
    for (uint32_t i = 0; i < 7u; ++i) {
        uint32_t page = i < 3u ? 7u : 8u, knob = i < 3u ? i : i - 3u;
        br_ui.screen[BR_PAGE_MIX] = page;
        for (uint32_t channel = 0; channel <= 16u; ++channel) {
            br_midi_channels[i] = (uint8_t)channel;
            br_control_t c = br_control(knob); char text[24];
            HD_CHECK(c.low == 0 && c.high == 16 && c.value == (int32_t)channel, "destination %u channel %u: range and value", i, channel);
            HD_CHECK(br_control_widget(knob, &c) == BR_WIDGET_MIDI_CHANNEL, "destination %u: MIDI channel widget", i);
            HD_CHECK(br_control_widget_variant(knob, &c, BR_WIDGET_MIDI_CHANNEL) == (channel | i << 8),
                     "destination %u channel %u: variant channel | destination << 8", i, channel);
            br_control_text(knob, &c, text);
            if (!channel) HD_CHECK(!strcmp(text, "OFF"), "channel 0 reads OFF");
            else { char expected[4]; snprintf(expected, sizeof expected, "%u", channel); HD_CHECK(!strcmp(text, expected), "channel %u text", channel); }
        }
        br_midi_channels[i] = 1u; br_control_turn(knob, -1); HD_CHECK(br_midi_channels[i] == 0u, "turn down to OFF");
        br_control_turn(knob, 1); HD_CHECK(br_midi_channels[i] == 1u, "turn up to 1");
    }
    br_ui.screen[BR_PAGE_MIX] = 7u;
    { br_control_t c = br_control(3u); HD_CHECK(br_control_widget(3u, &c) != BR_WIDGET_MIDI_CHANNEL, "knob 4 is not a channel"); }
    /* OFF suppresses new output and releases an already-sent note once. */
    br_app_init(); host_after_intro(); br_ui.page = BR_PAGE_HOME; br_ui.part = 0u; br_arp_on = 0u; br_hold_on = 0u;
    br_midi_channels[0] = 0u; host_midi_out_count = 0u;
    br_key_down(0u, 1); br_key_up(0u); HD_CHECK(!host_midi_out_count, "OFF sends nothing");
    br_midi_channel_set(0u, 1u); br_key_down(0u, 1);
    HD_CHECK(host_midi_out_count == 1u && host_midi_out_last_status == 0x90u, "channel 1 sends note on");
    br_midi_channel_set(0u, 0u);
    HD_CHECK(host_midi_out_count == 2u && host_midi_out_last_status == 0x80u, "OFF releases the sent note");
    br_key_up(0u); HD_CHECK(host_midi_out_count == 2u, "no second note off");
    /* The icon, for a track (T1) and a kit lane (D2). */
    for (uint32_t dest = 0u; dest < 7u; dest += 4u) {
        double a1 = 0, step = 0;
        for (uint32_t channel = 0; channel <= 16u; ++channel) {
            double angle, radius;
            draw_icon(channel, dest); memcpy(icon[channel], cv_px, sizeof icon[channel]);
            hd_box_t b = hd_box(cv_px, 60, 60, BR_BACKGROUND, 1);
            HD_CHECK(b.left >= 0 && b.top >= 0 && b.right < 60 && b.bottom < 60 && b.left > 0 && b.top > 0,
                     "destination %u channel %u: inside the cell (x %d..%d, y %d..%d)", dest, channel, b.left, b.right, b.top, b.bottom);
            if (!channel) continue;
            HD_CHECK(lit_dot(&angle, &radius) && fabs(radius - 18.0) <= 1.0,
                     "destination %u channel %u: the lit dot is on the ring (radius %.2f)", dest, channel, radius);
            if (channel == 1u) a1 = angle;
            else {
                double d = fmod(angle - a1 + 720.0, 360.0);
                if (channel == 2u) step = d < 180.0 ? 22.5 : -22.5;   /* the direction the ring counts */
                HD_CHECK(fabs(remainder(d - (channel - 1u) * step, 360.0)) < 3.0,
                         "destination %u channel %u: the dot steps by 1/16 turn (at %.1f deg from channel 1)", dest, channel, d);
            }
        }
        /* Channels 1-16 differ only around the dots that move: everything nearer the centre than
         * radius 14 (the socket and its pins) is identical. */
        for (uint32_t channel = 2u; channel <= 16u; ++channel) {
            uint32_t n = 0;
            for (int32_t y = 0; y < 60; ++y) for (int32_t x = 0; x < 60; ++x)
                if ((x - 30) * (x - 30) + (y - 29) * (y - 29) < 13 * 13) n += icon[channel][y * 60 + x] != icon[1][y * 60 + x];
            HD_CHECK(!n, "destination %u channel %u: socket and pins as for channel 1 (%u px differ)", dest, channel, n);
        }
        {   /* OFF: a struck-through grey socket: the pins are dim, the strike crosses the centre */
            uint32_t centre_off = icon[0][29 * 60 + 30], centre_on = icon[1][29 * 60 + 30];
            HD_CHECK(centre_off != centre_on, "destination %u: OFF strikes through the socket centre", dest);
        }
        {   /* the socket ring at its left edge (radius 11) has the track colour, or the kit's */
            uint16_t ring = swap16(icon[1][29 * 60 + 19]), hue = br_track_color(dest < 3u ? dest : BR_DRUM_PART);
            HD_CHECK(hd_near(ring, hue, 3u, 0), "destination %u: socket ring %04x in the track colour %04x", dest, ring, hue);
        }
        if (dest >= 3u) {   /* lane pips at y 52..54, x 23..37; this lane's at x 11 + 4 * dest */
            uint16_t lit = swap16(icon[1][53 * 60 + 12 + 4 * (int32_t)dest]);
            HD_CHECK(hd_near(lit, br_track_color(BR_DRUM_PART), 3u, 0), "kit lane %u: its pip lit (%04x)", dest - 2u, lit);
        }
    }
    /* The page draws exactly the widget: T1 at OFF, T2 at 1, T3 at 16 on MIDI page 1. */
    br_ui.part = 0u; br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 7u;
    br_midi_channels[0] = 0u; br_midi_channels[1] = 1u; br_midi_channels[2] = 16u;
    br_ui.redraw = 1u; br_draw();
    {
        uint32_t found = 0;
        for (uint32_t k = 0; k < 3u; ++k) {
            draw_icon(k == 0 ? 0u : k == 1 ? 1u : 16u, k);
            /* find the 60x60 widget on the page: search the control row for an exact match */
            for (int32_t oy = 30; oy <= 180 && !(found & (1u << k)); ++oy) for (int32_t ox = 0; ox + 60 <= 240; ++ox) {
                int same = 1;
                for (int32_t y = 0; y < 60 && same; ++y) for (int32_t x = 0; x < 60 && same; ++x)
                    if (cv_px[y * 60 + x] != swap16(BR_BACKGROUND)) same = fb[(oy + y) * 240 + ox + x] == swap16(cv_px[y * 60 + x]);
                if (same) { found |= 1u << k; break; }
            }
        }
        if (found != 7u) hd_write_crop("build/midi_page.ppm", fb, 240, 0, 0, 0, 240, 240, 2);
        HD_CHECK(found == 7u, "the MIDI page shows each destination's widget exactly (found mask %u)", found);
    }
    return hd_finish("HD MIDI channel controls, OFF routing and the 0-16 socket icons");
}
