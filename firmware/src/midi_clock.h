#ifndef FM1_MIDI_CLOCK_H
#define FM1_MIDI_CLOCK_H

/* Audio-owned MIDI clock follower. All timestamps are monotonically increasing
 * audio frames in the same epoch, including time spent stopped. Input events
 * must be delivered in timestamp order. No allocation or platform calls.
 *
 * Timing limitation: an observed long acquisition gap that could be either
 * jitter or a missed clock is refused rather than assigned an invented pulse.
 * A missed clock whose arrival gap overlaps ordinary jitter cannot always be
 * detected at all; in that case this follower may retain a phase error. The
 * source must provide sufficiently reliable F8 delivery for phase alignment.
 * ±3 ms seeded host jitter is covered; this is not a measured device limit. */
#include <stdint.h>
#include <string.h>
#include "midi_clock_math.h"

#define FM1_MC_START    1u
#define FM1_MC_CONTINUE 2u
#define FM1_MC_STOP     4u
#define FM1_MC_LOCKED   8u
#define FM1_MC_LOST    16u
#define FM1_MC_INVALID 32u
#define FM1_MC_SUSPECT 64u
#define FM1_MC_REJECTED 128u

typedef struct {
    uint64_t now, last_tick, wait_since, phase_q16, period_q16, phase_remainder;
    uint64_t probe_tick, probe_period_q16;
    uint64_t window_start, window_last, window_candidate_q16, acquisition_origin_q16;
    uint64_t acquisition_start_frame, acquisition_max_gap;
    uint32_t acquisition_ticks, acquisition_raw_intervals;
    uint32_t sample_rate, min_period, max_period;
    int32_t phase_adjust_q16;
    uint8_t have_tick, intervals, locked, lost, running, pending, multiple_run;
    uint8_t probe_active, probe_intervals, window_have, window_intervals, window_ticks, window_good;
} fm1_midi_clock_t;

/* The accepted range is 30..300 BPM, at 24 pulses per quarter, with a
 * two-frame interval margin for timestamp quantization and boundary jitter. */
static void fm1_mc_init(fm1_midi_clock_t *c, uint32_t sample_rate)
{
    memset(c, 0, sizeof *c);
    c->sample_rate = sample_rate;
    c->min_period = sample_rate / 120u; /* 300 BPM */
    c->max_period = (sample_rate + 11u) / 12u;  /* 30 BPM */
    c->min_period = c->min_period > 2u ? c->min_period - 2u : 1u;
    c->max_period += 2u;
    if (c->max_period < c->min_period) c->max_period = c->min_period;
}

/* Advances running musical phase in 1/65536 MIDI-clock pulses. Continue
 * advancing at the last estimate after loss: the owner decides whether to
 * stop/freeze. A single call must span no more than 0x00ffffff frames. */
static uint32_t fm1_mc_advance(fm1_midi_clock_t *c, uint64_t frame)
{
    uint64_t delta, timeout, advance, limit;
    int64_t adjustment;
    uint32_t result = 0u;
    if (frame < c->now || frame - c->now > 0x00ffffffu) return FM1_MC_INVALID;
    delta = frame - c->now;
    /* Acquisition has no trusted rate. Keep phase frozen until the
     * validated raw-clock count can advance it, without a backward snap. */
    if (c->running && c->locked && c->period_q16) {
        uint64_t numerator = (delta << 32) + c->phase_remainder;
        advance = fm1_clock_div64(numerator, c->period_q16, &c->phase_remainder);
        limit = advance / 8u;
        adjustment = c->phase_adjust_q16;
        if (adjustment > (int64_t)limit) adjustment = (int64_t)limit;
        if (adjustment < -(int64_t)limit) adjustment = -(int64_t)limit;
        c->phase_q16 += (uint64_t)((int64_t)advance + adjustment);
        c->phase_adjust_q16 -= (int32_t)adjustment;
    }
    c->now = frame;
    if (c->have_tick || c->pending) {
        /* Four predicted clock intervals, with 100 ms minimum, avoids a
         * single late pulse falsely declaring loss at high tempos. */
        /* An unlocked estimate can be far from the source tempo. Use the
         * slowest supported clock for acquisition/loss detection. */
        timeout = c->locked && c->period_q16 ? (4u * c->period_q16 + 65535u) >> 16 :
                                               4u * (uint64_t)c->max_period;
        if (timeout < c->sample_rate / 10u) timeout = c->sample_rate / 10u;
        if (frame - (c->have_tick ? c->last_tick : c->wait_since) > timeout) {
            c->have_tick = c->intervals = c->locked = 0u;
            c->probe_active = c->probe_intervals = 0u;
            c->window_have = c->window_intervals = c->window_ticks = c->window_good = 0u;
            if (!c->lost) { c->lost = 1u; result |= FM1_MC_LOST; }
        }
    }
    return result;
}

/* Returns action flags. START/CONTINUE become effective on the next F8;
 * STOP is immediate. Repeated/reordered timestamps must be rejected upstream
 * except simultaneous ordered events, which are allowed. */
static uint32_t fm1_mc_event(fm1_midi_clock_t *c, uint8_t status, uint64_t frame)
{
    uint32_t result = fm1_mc_advance(c, frame);
    uint64_t elapsed, candidate, multiples, estimate, raw_elapsed = 0u;
    int32_t error, correction;
    if (result & FM1_MC_INVALID) return result;
    if (status == 0xFAu || status == 0xFBu) {
        if (c->running) result |= FM1_MC_STOP;
        c->running = 0u;
        c->probe_active = c->probe_intervals = 0u;
        c->pending = status;
        c->wait_since = frame;
        return result;
    }
    if (status == 0xFCu) {
        c->running = 0u; c->pending = 0u;
        c->probe_active = c->probe_intervals = 0u;
        return result | FM1_MC_STOP;
    }
    if (status != 0xF8u) return result | FM1_MC_INVALID;
    /* Estimate tempo from a whole beat of received pulses. Independent
     * arrival jitter then affects only the two window endpoints, rather
     * than biasing an EMA toward long individually accepted intervals.
     * Every ordered F8 contributes, even when its individual interval is
     * too noisy for phase tracking. */
    if (!c->window_have) {
        c->window_have = 1u; c->window_start = c->window_last = frame;
        c->window_intervals = c->window_ticks = 0u;
    } else {
        uint64_t window_period;
        uint32_t elapsed_ticks = 1u;
        if (frame <= c->window_last) return result | FM1_MC_INVALID;
        raw_elapsed = frame - c->window_last;
        c->window_last = frame;
        /* A clearly long gap can stand for missed clocks. A 7/4 threshold
         * avoids mistaking the supported ±3 ms jitter at 300 BPM for a
         * missing pulse; ambiguous gaps remain one observation. */
        if (c->intervals >= 3u && c->period_q16 &&
            (raw_elapsed << 16) >= c->period_q16 * 7u / 4u) {
            uint64_t rounded = (raw_elapsed << 16) + (c->period_q16 >> 1);
            elapsed_ticks = rounded >= 4u*c->period_q16 ? 4u :
                            rounded >= 3u*c->period_q16 ? 3u : 2u;
        }
        if (!c->locked && c->running) {
            if (raw_elapsed > c->acquisition_max_gap) c->acquisition_max_gap = raw_elapsed;
            if (c->acquisition_raw_intervals == UINT32_MAX)
                return result | FM1_MC_INVALID;
            ++c->acquisition_raw_intervals;
        }
        c->window_ticks = (uint8_t)(c->window_ticks + elapsed_ticks);
        if (++c->window_intervals == 24u) {
            window_period = fm1_clock_div64((frame - c->window_start) << 16, c->window_ticks, 0);
            if (window_period >= (uint64_t)(c->min_period - c->min_period / 25u) * 65536u &&
                window_period <= (uint64_t)(c->max_period + c->max_period / 25u) * 65536u) {
                /* Endpoints can move a little outside the supported range
                 * due to arrival jitter; clamp only the window estimate. */
                if (window_period < ((uint64_t)c->min_period << 16))
                    window_period = (uint64_t)c->min_period << 16;
                if (window_period > ((uint64_t)c->max_period << 16))
                    window_period = (uint64_t)c->max_period << 16;
                if (!c->locked) {
                    if (c->window_good &&
                        window_period >= c->window_candidate_q16 * 7u / 8u &&
                        window_period <= c->window_candidate_q16 * 9u / 8u) {
                        c->period_q16 = (c->window_candidate_q16 + window_period) / 2u;
                        c->phase_remainder = 0u;
                        if (c->running) {
                            /* Count observed edges directly. A long gap may
                             * indicate a missing pulse or excessive arrival
                             * jitter; do not silently guess in the overlap. */
                            c->acquisition_ticks = c->acquisition_raw_intervals;
                            if ((c->acquisition_max_gap << 16) >= c->period_q16 * 7u / 4u) {
                                uint64_t period_frames = (c->period_q16 + 65535u) >> 16;
                                uint64_t jitter_span = (uint64_t)c->sample_rate * 8u / 1000u + 2u;
                                uint64_t inferred;
                                if (c->acquisition_max_gap <=
                                    period_frames + jitter_span + period_frames / 16u)
                                    return result | FM1_MC_INVALID;
                                inferred = fm1_clock_div64(
                                    ((frame - c->acquisition_start_frame) << 16) + c->period_q16 / 2u,
                                    c->period_q16, 0);
                                if (inferred <= c->acquisition_raw_intervals ||
                                    inferred - c->acquisition_raw_intervals > 4u || inferred > UINT32_MAX)
                                    return result | FM1_MC_INVALID;
                                c->acquisition_ticks = (uint32_t)inferred;
                            }
                            c->phase_q16 = c->acquisition_origin_q16 + ((uint64_t)c->acquisition_ticks << 16);
                        }
                        c->phase_adjust_q16 = 0;
                        c->locked = 1u; c->lost = 0u; result |= FM1_MC_LOCKED;
                        c->window_good = 0u;
                    } else {
                        c->window_candidate_q16 = window_period;
                        c->window_good = 1u;
                    }
                } else if (c->period_q16 &&
                           window_period >= c->period_q16 * 3u / 4u &&
                           window_period <= c->period_q16 * 5u / 4u) {
                    c->period_q16 = (3u * c->period_q16 + window_period) / 4u;
                    c->phase_remainder = 0u;
                }
            } else if (!c->locked) c->window_good = 0u;
            c->window_start = frame; c->window_intervals = c->window_ticks = 0u;
        }
    }
    /* Transport starts on the first subsequent ordered F8 even if that
     * interval is too noisy for tempo estimation. The audio owner still
     * waits for lock before rendering when acquisition is incomplete. */
    if (c->pending) {
        if (c->pending == 0xFAu) {
            c->phase_q16 = 0u; c->phase_remainder = 0u;
            c->phase_adjust_q16 = 0; result |= FM1_MC_START;
        } else result |= FM1_MC_CONTINUE;
        c->pending = 0u; c->running = 1u;
        c->last_tick = frame; c->have_tick = 1u;
        if (!c->locked) {
            c->acquisition_origin_q16 = c->phase_q16;
            c->acquisition_ticks = c->acquisition_raw_intervals = 0u;
            c->acquisition_start_frame = frame; c->acquisition_max_gap = 0u;
        }
        return result;
    }
    if (c->probe_active) {
        /* A run of early pulses may be a faster clock, or an extra pulse.
         * Keep the old period until three consecutive consistent intervals
         * establish the new one; the owner must hold/freeze while suspect. */
        elapsed = frame - c->probe_tick;
        if (!elapsed || elapsed > UINT32_MAX) return result | FM1_MC_INVALID;
        candidate = elapsed << 16;
        if (candidate >= ((uint64_t)c->min_period << 16) &&
            candidate <= ((uint64_t)c->max_period << 16) &&
            candidate < c->period_q16 * 3u / 4u &&
            (!c->probe_intervals ||
             (candidate >= c->probe_period_q16 * 3u / 4u &&
              candidate <= c->probe_period_q16 * 5u / 4u))) {
            c->probe_tick = frame;
            c->probe_period_q16 = c->probe_intervals ?
                (3u * c->probe_period_q16 + candidate) / 4u : candidate;
            if (++c->probe_intervals == 3u) {
                c->locked = 0u;
                c->acquisition_origin_q16 = c->phase_q16;
                c->acquisition_ticks = c->acquisition_raw_intervals = 0u; c->window_good = 0u;
                c->acquisition_start_frame = frame; c->acquisition_max_gap = 0u;
                c->period_q16 = c->probe_period_q16;
                c->phase_remainder = 0u;
                c->last_tick = frame;
                c->have_tick = 1u;
                c->intervals = 1u;
                c->multiple_run = 0u;
                c->probe_active = c->probe_intervals = 0u;
                c->window_start = frame; c->window_intervals = c->window_ticks = 0u;
                return result | FM1_MC_SUSPECT;
            }
            return result | FM1_MC_REJECTED;
        }
        /* Probe did not persist: reconsider this pulse against the last
         * valid clock, then reacquire lock on the original tempo. */
        c->probe_active = c->probe_intervals = 0u;
    }
    if (c->have_tick) {
        elapsed = frame - c->last_tick;
        if (!elapsed || elapsed > UINT32_MAX) return result | FM1_MC_INVALID;
        candidate = elapsed << 16;
        if (c->period_q16 && c->intervals && c->locked &&
            candidate >= ((uint64_t)c->min_period << 16) &&
            candidate < c->period_q16 * 3u / 4u) {
            c->probe_tick = frame;
            c->probe_period_q16 = 0u;
            c->probe_intervals = 0u;
            c->probe_active = 1u;
            c->multiple_run = 0u;
            return result | FM1_MC_REJECTED;
        }
        multiples = 1u;
        if (c->period_q16 && c->intervals) {
            /* Round to the nearest interval, clamped to 1..4; comparisons
             * avoid a wide division for every received MIDI clock. */
            uint64_t rounded = candidate + (c->period_q16 >> 1);
            multiples = rounded >= 4u * c->period_q16 ? 4u :
                        rounded >= 3u * c->period_q16 ? 3u :
                        rounded >= 2u * c->period_q16 ? 2u : 1u;
            if (c->locked && multiples > 1u && (raw_elapsed << 16) >= c->period_q16 * 3u / 2u &&
                candidate <= ((uint64_t)c->max_period << 16)) {
                if (c->multiple_run < 3u) ++c->multiple_run;
            } else c->multiple_run = 0u;
            if (c->multiple_run < 3u) {
                if (multiples == 2u) candidate >>= 1;
                else if (multiples == 4u) candidate >>= 2;
                else if (multiples == 3u) candidate = fm1_clock_div64(candidate, 3u, 0);
            }
        }
        if (candidate < ((uint64_t)c->min_period << 15) ||
            candidate > ((uint64_t)c->max_period << 16) * 9u / 8u ||
            (c->period_q16 && c->intervals && c->multiple_run < 3u &&
             (candidate < c->period_q16 / 2u ||
                               candidate > c->period_q16 * 3u / 2u))) {
            /* While acquiring, ordered raw clocks still prove the source is
             * present even if the provisional period is far from reality. */
            if (!c->locked) { c->last_tick = frame; c->have_tick = 1u; }
            return result | FM1_MC_REJECTED;
        }
        /* A single interval near a tempo endpoint can exceed the bounds
         * solely due to jitter. Initial acquisition uses a bounded estimate;
         * the longer window subsequently controls steady-state tempo. */
        if (!c->locked && candidate < ((uint64_t)c->min_period << 16))
            candidate = (uint64_t)c->min_period << 16;
        if (!c->locked && candidate > ((uint64_t)c->max_period << 16))
            candidate = (uint64_t)c->max_period << 16;
        estimate = c->period_q16;
        if (!estimate && c->running) c->phase_q16 += multiples * 65536u;
        c->period_q16 = estimate && c->intervals && c->multiple_run < 3u ?
                          (c->locked ? estimate : (7u * estimate + candidate) / 8u) : candidate;
        if (c->period_q16 != estimate) c->phase_remainder = 0u;
        if (c->multiple_run == 3u) {
            c->multiple_run = 0u;
            c->locked = 0u;
            c->acquisition_origin_q16 = c->phase_q16;
            c->acquisition_ticks = c->acquisition_raw_intervals = 0u; c->window_good = 0u;
            c->acquisition_start_frame = frame; c->acquisition_max_gap = 0u;
            c->intervals = 0u;
            c->window_start = frame; c->window_intervals = c->window_ticks = 0u;
            result |= FM1_MC_SUSPECT;
        }
        if (c->intervals < 3u) ++c->intervals;
    }
    c->last_tick = frame; c->have_tick = 1u;
    if (c->running && c->locked && c->period_q16) {
        /* Slew a quarter of the nearest pulse phase error over future frames.
         * The bounded correction cannot make musical phase run backwards. */
        error = (int32_t)(c->phase_q16 & 65535u);
        if (error > 32768) error -= 65536;
        correction = -error / 4;
        if (correction > 8192) correction = 8192;
        if (correction < -8192) correction = -8192;
        c->phase_adjust_q16 = correction;
    }
    return result;
}
#endif
