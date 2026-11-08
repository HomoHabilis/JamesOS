/* SPDX-License-Identifier: GPL-3.0-only */
/* Legacy IDs 0..51 remain named for save/restore compatibility. Track-role
 * selection exposes Plaits models 52..75 and kits 48..50/76 only. */
#include "instrument.h"
static const char *const br_names[BR_PRESET_COUNT] = {
    "CSAW", "MORPH", "SAW SQUARE", "SINE TRI", "BUZZ", "SQUARE SUB",
    "SAW SUB", "SQUARE SYNC", "SAW SYNC", "TRIPLE SAW", "TRIPLE SQR",
    "TRIPLE TRI", "TRIPLE SINE", "RING", "SAW SWARM", "SAW COMB", "TOY",
    "LP FILTER", "PEAK FILTER", "BAND FILTER", "HIGH FILTER", "VOSIM",
    "VOWEL", "VOWEL FOF", "HARMONICS", "FM", "FEEDBACK FM", "CHAOTIC FM",
    "PLUCKED", "BOWED", "BLOWN", "FLUTED", "BELL", "DRUM", "KICK",
    "CYMBAL", "SNARE", "WAVETABLE", "WAVE MAP", "WAVE LINE",
    "PARA WAVE", "NOISE FILTER", "TWIN PEAKS", "CLOCK NOISE",
    "GRAN CLOUD", "PARTICLE", "QPSK", "QUESTION", "BRAIDS KIT",
    "80S KIT", "90S KIT", "RESERVED"
#if BR_ENABLE_PLAITS
    ,
    "PL ANALOG VCF", "PL PHASE DIST", "PL FM BASS", "PL FM KEYS",
    "PL FM PAD", "PL WAVE TERRAIN", "PL STRING MACH", "PL CHIPTUNE",
    "PL VIRTUAL AN", "PL WAVESHAPE", "PL FM", "PL GRAIN",
    "PL HARMONIC", "PL WAVETABLE", "PL CHORD", "PL SPEECH",
    "PL SWARM", "PL NOISE", "PL PARTICLE", "PL STRING",
    "PL MODAL", "PL KICK", "PL SNARE", "PL HI HAT", "PL PLAITS KIT"
#endif
};

typedef struct {
    uint8_t timbre, color, attack, decay, sustain, release;
} br_factory_t;

/* Values stay byte-sized for the app's compact save/UI controls. */
static const br_factory_t br_factory[48] = {
    {74,62,2,30,112,24}, {42,78,4,34,104,28}, {60,88,2,24,116,22},
    {48,64,3,30,108,26}, {80,72,2,28,106,24}, {52,44,2,26,118,22},
    {58,58,2,28,116,22}, {68,74,2,25,108,20}, {72,68,2,26,108,20},
    {44,88,4,32,100,26}, {52,78,3,30,104,24}, {48,62,5,36,102,30},
    {48,58,5,38,100,32}, {84,56,2,32,108,24}, {70,82,3,28,108,22},
    {76,88,2,32,106,22}, {48,66,2,30,100,22}, {46,78,3,34,104,26},
    {56,86,3,32,102,24}, {58,84,3,30,102,24}, {52,72,3,30,102,24},
    {60,72,2,34,102,24}, {48,84,3,38,102,30}, {58,74,2,36,100,28},
    {52,82,5,38,100,30}, {62,72,2,30,108,22}, {58,86,2,32,102,24},
    {66,78,2,30,98,22}, {70,68,0,20,127,88}, {54,70,6,28,127,72},
    {48,74,5,34,127,72}, {44,64,8,42,127,72}, {62,82,0,18,127,96},
    {76,72,0,16,127,88}, {78,64,0,16,127,88}, {62,92,0,14,127,96},
    {72,74,0,15,127,88}, {52,72,4,32,104,26}, {60,68,4,34,104,26},
    {58,76,3,32,104,24}, {50,68,3,32,108,24}, {58,82,2,28,94,22},
    {54,82,2,28,96,22}, {56,70,2,28,94,20}, {56,76,4,38,100,28},
    {58,82,3,34,98,26}, {60,64,2,28,100,22}, {64,64,3,30,100,24}
};

/* Harmonics, Timbre and Morph keep Plaits' three independent macro controls. */
static const uint8_t br_plaits_factory[BR_PLAITS_MODEL_COUNT][3] = {
    {48, 76, 64}, {64, 64, 64}, {12, 76, 64}, {12, 76, 64},
    {12, 76, 64}, {32, 64, 64}, {48, 64, 64}, {64, 64, 64},
    {32, 64, 64}, {64, 76, 64}, {48, 64, 64}, {64, 64, 64},
    {64, 76, 64}, {32, 64, 64}, {48, 64, 64}, {64, 64, 64},
    {32, 76, 100}, {64, 76, 64}, {64, 76, 64}, {48, 76, 76},
    {64, 76, 76}, {64, 76, 76}, {64, 76, 76}, {64, 76, 76}
};

BR_AUDIO_API const char *br_preset_name(uint32_t preset)
{
    return br_names[preset < BR_PRESET_COUNT ? preset : 0u];
}

BR_AUDIO_API void br_preset_sound(uint32_t preset, br_sound_t *sound)
{
    const br_factory_t *f;
    if (!sound) return;
    if (preset >= BR_PRESET_COUNT) preset = 0;
    sound->preset = preset;
    sound->tune = 0;
    sound->level = 127;
    sound->filter = 64;
    sound->filter_resonance = 0u;
    sound->filter_keytrack = 0u;
    sound->timbre_mod = sound->fm_mod = sound->morph_mod = 64u;
    sound->pan = 64;
    sound->drum_snap = 64;
    sound->drum_voice_mask = 0; sound->drum_key_lane = 0;
    sound->plaits_harmonics = 64; sound->plaits_aux = 64;
    sound->drive = sound->delay = sound->reverb = sound->space = 0;
    sound->mute = 0;
    sound->gate = BR_GATE_OPEN;
    if (br_is_plaits(preset)) {
        uint32_t model = preset - BR_FIRST_PLAITS;
        sound->plaits_harmonics = br_plaits_factory[model][0];
        sound->timbre = br_plaits_factory[model][1];
        sound->color = br_plaits_factory[model][2];
        sound->attack = 0; sound->decay = 64;
        sound->sustain = 127; sound->release = 40;
        return;
    }
    if (br_is_kit(preset)) {
        sound->timbre = sound->color = 64;
        /* The amplitude envelope and each hit's final gate are independent. */
        sound->attack = 0;
        sound->decay = preset == BR_KIT_808 || preset == BR_KIT_909 ? 127u : 64u;
        sound->sustain = 127; sound->release = 64;
        return;
    }
    f = &br_factory[preset];
    sound->timbre = f->timbre; sound->color = f->color;
    sound->attack = f->attack; sound->decay = f->decay;
    sound->sustain = f->sustain; sound->release = f->release;
}
