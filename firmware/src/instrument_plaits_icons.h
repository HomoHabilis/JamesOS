/* Plaits symbols adapted from Mutable Instruments documentation (CC BY-SA 3.0).
 * https://github.com/pichenettes/mutable-instruments-documentation
 * Resized and converted to monochrome for the display. */
#ifndef BR_PLAITS_ICONS_H
#define BR_PLAITS_ICONS_H
/* HD: the same sixteen symbols as anti-aliased line art. Per icon, a list of
 * items ending in 0: n (1..31) then n points for a polyline; 0x40|r then
 * cx, cy for a ring; 0x80|r then cx, cy for a disc. Coordinates and radii are
 * in half pixels within the 20 x 20 chip (an even value is a pixel centre). */
static const uint8_t br_plaits_vec[] = {
    /*  0 virtual analog: saw over pulse */ 5, 8, 17, 19, 8, 19, 17, 30, 8, 30, 17, 6, 7, 31, 13, 31, 13, 23, 25, 23, 25, 31, 31, 31, 0,
    /*  1 waveshaping: a bent sine */ 9, 6, 20, 9, 13, 12, 10, 16, 13, 19, 20, 22, 27, 26, 30, 29, 27, 32, 20, 0,
    /*  2 FM: two operators */ 71, 14, 14, 5, 23, 23, 32, 23, 32, 32, 23, 32, 23, 23, 2, 19, 19, 23, 23, 0,
    /*  3 formant / grain: a peak */ 5, 5, 27, 11, 27, 19, 11, 27, 27, 33, 27, 0,
    /*  4 harmonic: stacked partials */ 5, 6, 13, 12, 9, 19, 13, 26, 9, 32, 13, 5, 6, 22, 12, 18, 19, 22, 26, 18, 32, 22, 5, 6, 31, 12, 27, 19, 31, 26, 27, 32, 31, 0,
    /*  5 wavetable: a grid */ 5, 7, 9, 31, 9, 31, 29, 7, 29, 7, 9, 2, 15, 9, 15, 29, 2, 23, 9, 23, 29, 2, 7, 16, 31, 16, 2, 7, 22, 31, 22, 0,
    /*  6 chords: stacked notes */ 132, 20, 30, 132, 20, 22, 132, 20, 14, 2, 24, 30, 24, 6, 0,
    /*  7 speech: a bubble */ 76, 19, 17, 3, 12, 27, 9, 34, 19, 29, 0,
    /*  8 swarm: a cloud */ 134, 13, 24, 136, 20, 19, 134, 27, 24, 133, 20, 26, 0,
    /*  9 noise: spikes */ 9, 5, 22, 9, 14, 12, 28, 16, 8, 19, 31, 22, 11, 25, 27, 29, 16, 33, 22, 0,
    /* 10 particles: dust */ 130, 10, 12, 130, 18, 8, 131, 27, 14, 131, 14, 21, 130, 23, 22, 131, 9, 28, 130, 29, 29, 130, 19, 31, 0,
    /* 11 string: a plucked string */ 2, 19, 5, 19, 33, 3, 13, 11, 11, 19, 13, 27, 3, 25, 11, 27, 19, 25, 27, 3, 7, 15, 6, 19, 7, 23, 3, 31, 15, 32, 19, 31, 23, 0,
    /* 12 modal: resonant rings */ 78, 19, 19, 73, 19, 19, 132, 19, 19, 0,
    /* 13 bass drum: shell and beater */ 76, 21, 21, 132, 21, 21, 2, 6, 6, 13, 13, 131, 13, 13, 0,
    /* 14 snare: drum and sticks */ 5, 7, 19, 31, 19, 31, 31, 7, 31, 7, 19, 2, 7, 25, 31, 25, 2, 7, 6, 18, 16, 2, 31, 6, 20, 16, 0,
    /* 15 hi-hat: two cymbals on a stand */ 2, 19, 6, 19, 34, 4, 6, 17, 19, 12, 32, 17, 6, 17, 4, 6, 22, 32, 22, 19, 27, 6, 22, 0,
};
#endif
