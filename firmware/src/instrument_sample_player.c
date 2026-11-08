/* SPDX-License-Identifier: GPL-3.0-only */
#include "instrument_sample_player.h"
#include "../hal/fm1_audio_rate.h"
#include <string.h>

/* The generator emits immutable payload arrays and the single descriptor
 * table definition here, keeping the large bank in const/XIP storage. */
#include "../../assets/drums/generated/drum_sample_bank_data.inc"

enum { BR_IMA_BLOCK_FRAMES = 256u, BR_IMA_BLOCK_BYTES = 132u };

static const int16_t br_ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t br_ima_index[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint32_t br_tune_step_q16[73] = {
    8192u, 8679u, 9195u, 9742u, 10321u, 10935u, 11585u, 12274u,
    13004u, 13777u, 14596u, 15464u, 16384u, 17358u, 18390u, 19484u,
    20643u, 21870u, 23170u, 24548u, 26008u, 27554u, 29193u, 30929u,
    32768u, 34716u, 36781u, 38968u, 41285u, 43740u, 46341u, 49097u,
    52016u, 55109u, 58386u, 61858u, 65536u, 69433u, 73562u, 77936u,
    82570u, 87480u, 92682u, 98193u, 104032u, 110218u, 116772u, 123715u,
    131072u, 138866u, 147123u, 155872u, 165140u, 174960u, 185364u,
    196386u, 208064u, 220436u, 233544u, 247431u, 262144u, 277732u,
    294247u, 311744u, 330281u, 349920u, 370728u, 392772u, 416128u,
    440872u, 467088u, 494862u,
    524288u
};

static uint32_t br_sample_step_q16(const br_drum_sample_desc_t *sample,
                                   uint32_t tune_index)
{
    /* Table values are source frames/output frame for 44.1 kHz output.
     * Preserve the asset's 44.1 kHz clock when the DAC rate changes. */
    return (uint32_t)(((uint64_t)br_tune_step_q16[tune_index] * sample->rate +
                       FM1_AUDIO_RATE_HZ / 2u) / FM1_AUDIO_RATE_HZ);
}

static uint32_t br_sample_duration(const br_drum_sample_desc_t *sample,
                                   uint32_t decay)
{
    uint32_t duration;
    if (decay > 127u) decay = 127u;
    duration = decay == 127u ? sample->frames : 441u + decay * decay * 8u;
    if (duration > sample->frames) duration = sample->frames;
    return duration ? duration : 1u;
}

static int16_t br_read_i16le(const uint8_t *p)
{
    uint16_t bits = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return (int16_t)bits;
}

static int32_t br_clamp16(int32_t x)
{
    return x < -32768 ? -32768 : x > 32767 ? 32767 : x;
}

static int br_sample_valid(const br_drum_sample_desc_t *sample)
{
    if (!sample || !sample->data || !sample->frames || sample->rate != 44100u)
        return 0;
    if (sample->codec == BR_DRUM_SAMPLE_CODEC_PCM8)
        return sample->bytes == sample->frames;
    if (sample->codec == BR_DRUM_SAMPLE_CODEC_PCM8_PACKED) {
        uint32_t blocks = (sample->frames + 31u) >> 5;
        uint32_t table = (blocks + 1u) * 2u;
        if (sample->bytes < table + blocks * 2u) return 0;
        return (uint32_t)(uint16_t)br_read_i16le(sample->data + blocks * 2u)
            == sample->bytes - table;
    }
    if (sample->codec == BR_DRUM_SAMPLE_CODEC_IMA4) {
        uint32_t blocks = (sample->frames + BR_IMA_BLOCK_FRAMES - 1u) /
                          BR_IMA_BLOCK_FRAMES;
        return sample->bytes == blocks * BR_IMA_BLOCK_BYTES;
    }
    return 0;
}

static int br_decode_to(br_drum_sample_player_t *p, uint32_t frame,
                        int32_t *sample)
{
    uint32_t block, local;
    const uint8_t *data;
    if (!p->sample || frame >= p->sample->frames) return 0;
    if (p->codec == BR_DRUM_SAMPLE_CODEC_PCM8) {
        *sample = (int32_t)p->sample->data[frame] * 256 - 32768;
        return 1;
    }
    if (p->codec == BR_DRUM_SAMPLE_CODEC_PCM8_PACKED) {
        block = frame >> 5;
        if (!p->block_ready || p->decoded_block != block) {
            uint32_t blocks = (p->sample->frames + 31u) >> 5;
            uint32_t table = (blocks + 1u) * 2u;
            uint32_t offset = (uint16_t)br_read_i16le(p->sample->data + block * 2u);
            if (offset > p->sample->bytes - table - 2u) return 0;
            const uint8_t *packed = p->sample->data + table + offset;
            uint32_t count = p->sample->frames - block * 32u;
            if (count > 32u) count = 32u;
            if (packed[1] > 8u || table + offset + 2u + ((count * packed[1] + 7u) >> 3) > p->sample->bytes)
                return 0;
            /* Reuse the IMA decoder cache fields; codecs never coexist in
             * one player. Cache each block header without adding SRAM. */
            p->index = packed[1];
            p->predictor = packed[0];
            p->decoded_frame = table + offset + 2u;
            p->decoded_block = block;
            p->block_ready = 1u;
        }
        uint32_t bits = p->index, bit = (frame & 31u) * bits;
        uint32_t value = 0u;
        if (bits) {
            value = p->sample->data[p->decoded_frame + (bit >> 3)];
            if ((bit & 7u) + bits > 8u)
                value |= (uint32_t)p->sample->data[p->decoded_frame + 1u + (bit >> 3)] << 8;
            value = (value >> (bit & 7u)) & ((1u << bits) - 1u);
        }
        *sample = (int32_t)(p->predictor + value) * 256 - 32768;
        return 1;
    }
    block = frame / BR_IMA_BLOCK_FRAMES;
    local = frame % BR_IMA_BLOCK_FRAMES;
    data = p->sample->data + block * BR_IMA_BLOCK_BYTES;
    if (p->previous_block_valid && block == p->previous_block &&
        local == BR_IMA_BLOCK_FRAMES - 1u) {
        *sample = p->previous_block_last;
        return 1;
    }
    if (p->block_ready && p->decoded_block == block &&
        p->decoded_frame && local + 1u == p->decoded_frame) {
        *sample = p->previous_sample;
        return 1;
    }
    if (!p->block_ready || p->decoded_block != block || local < p->decoded_frame) {
        if (data[2] > 88u || data[3] != 0u) return 0;
        if (p->block_ready && block == p->decoded_block + 1u &&
            p->decoded_frame == BR_IMA_BLOCK_FRAMES - 1u) {
            p->previous_block = p->decoded_block;
            p->previous_block_last = p->predictor;
            p->previous_block_valid = 1u;
        }
        p->predictor = br_read_i16le(data);
        p->index = data[2];
        p->decoded_block = block;
        p->decoded_frame = 0u;
        p->block_ready = 1u;
    }
    while (p->decoded_frame < local) {
        uint32_t delta = p->decoded_frame;
        uint8_t packed = data[4u + (delta >> 1)];
        uint32_t code = (delta & 1u) ? (packed >> 4) : (packed & 15u);
        int32_t step = br_ima_step[p->index];
        int32_t diff = step >> 3;
        if (code & 1u) diff += step >> 2;
        if (code & 2u) diff += step >> 1;
        if (code & 4u) diff += step;
        int32_t next_index;
        p->previous_sample = p->predictor;
        p->predictor = br_clamp16(p->predictor + ((code & 8u) ? -diff : diff));
        next_index = (int32_t)p->index + br_ima_index[code & 7u];
        if (next_index < 0) next_index = 0;
        if (next_index > 88) next_index = 88;
        p->index = (uint8_t)next_index;
        ++p->decoded_frame;
    }
    *sample = p->predictor;
    return 1;
}

void br_drum_sample_player_reset(br_drum_sample_player_t *player)
{
    if (player) memset(player, 0, sizeof *player);
}

int br_drum_sample_player_start(br_drum_sample_player_t *player,
                                const br_drum_sample_desc_t *sample,
                                int32_t tune, uint32_t decay)
{
    uint32_t duration;
    if (!player) return 0;
    br_drum_sample_player_reset(player);
    if (!br_sample_valid(sample)) return 0;
    if (tune < -36) tune = -36;
    if (tune > 36) tune = 36;
    if (decay > 127u) decay = 127u;
    player->sample = sample;
    player->codec = (uint8_t)sample->codec;
    player->tune_index = (uint8_t)(tune + 36);
    player->step_q16 = br_sample_step_q16(sample, player->tune_index);
    player->decay = (uint8_t)decay;
    /* The factory maximum means the full original asset. Lower settings use
     * the instrument's existing quadratic decay-time curve, then a 64-frame
     * end fade. */
    duration = br_sample_duration(sample, decay);
    player->end_frame = duration;
    if (decay < 127u)
        player->fade_start_frame = duration > 64u ? duration - 64u : 0u;
    player->active = 1u;
    return 1;
}

void br_drum_sample_player_update_controls(br_drum_sample_player_t *player,
                                           int32_t tune, uint32_t decay)
{
    uint32_t tune_index, source_remaining, duration;
    if (!player || !player->active || !player->sample) return;
    if (tune < -36) tune = -36;
    if (tune > 36) tune = 36;
    if (decay > 127u) decay = 127u;

    tune_index = (uint32_t)(tune + 36);
    if (player->tune_index != tune_index) {
        player->tune_index = (uint8_t)tune_index;
        player->step_q16 = br_sample_step_q16(player->sample, tune_index);
    }

    /* Do not move a started closing fade: a later decay increase cannot
     * revive a voice after its end ramp has become audible. */
    if (player->decay == decay || player->fade_started ||
        player->frame >= player->sample->frames)
        return;
    source_remaining = player->sample->frames - player->frame;
    duration = decay == 127u ? source_remaining : br_sample_duration(player->sample, decay);
    if (duration > source_remaining) duration = source_remaining;
    player->end_frame = player->frame + duration;
    if (decay < 127u) {
        uint32_t fade_length = duration > 64u ? 64u : duration;
        player->fade_start_frame = player->end_frame - fade_length;
    } else {
        player->fade_start_frame = 0u;
    }
    player->decay = (uint8_t)decay;
}

int br_drum_sample_player_next(br_drum_sample_player_t *player,
                               int32_t *sample)
{
    int32_t a, b;
    uint32_t next_frame, sum;
    if (!player || !sample || !player->active || !player->sample ||
        player->frame >= player->end_frame) {
        if (player) player->active = 0u;
        if (sample) *sample = 0;
        return 0;
    }
    if (!br_decode_to(player, player->frame, &a)) {
        player->active = 0u; *sample = 0; return 0;
    }
    next_frame = player->frame + 1u;
    if (next_frame < player->end_frame && br_decode_to(player, next_frame, &b)) {
        int64_t delta = (int64_t)(b - a) * player->fraction;
        a += (int32_t)(delta >> 16);
    }
    if (player->fade_start_frame && player->frame >= player->fade_start_frame) {
        uint32_t span = player->end_frame - player->fade_start_frame;
        uint32_t remaining = player->end_frame - player->frame;
        player->fade_started = 1u;
        /* a is a signed 16-bit decoded/interpolated sample and the fade is
         * bounded to 64 frames, so this product is safely within int32_t.
         * Keep signed division's truncation-toward-zero behavior exact while
         * avoiding a target __divdi3 helper for the variable span. */
        a = (a * (int32_t)remaining) / (int32_t)span;
    } else if (player->fade_start_frame == 0u && player->end_frame < player->sample->frames) {
        uint32_t span = player->end_frame;
        uint32_t remaining = player->end_frame - player->frame;
        player->fade_started = 1u;
        a = (a * (int32_t)remaining) / (int32_t)span;
    }
    *sample = a;
    sum = (uint32_t)player->fraction + player->step_q16;
    player->frame += sum >> 16;
    player->fraction = (uint16_t)sum;
    return 1;
}
