/* Private whole-audio translation unit for the O3-vs-OS split experiment. */
#define BR_SPLIT_AUDIO 1
#include "../hal/fm1_audio_rate.h"
#define FS FM1_AUDIO_RATE_HZ
#include "instrument.h"

br_part_t br_part[BR_PARTS];
volatile uint32_t br_bpm = 120u, br_playing;
volatile uint8_t br_midi_channels[7] = {1,2,3,4,5,6,7};
volatile uint8_t br_midi_clock_enabled, br_midi_clock_active;
volatile uint32_t br_midi_clock_bpm;
volatile uint32_t br_midi_clock_generation;
volatile uint32_t br_transport_phase;
volatile uint32_t br_solo_mask, br_global_beat;
volatile uint32_t br_step_pos[BR_PARTS];
volatile uint32_t br_quiet_part[BR_PARTS];
volatile uint32_t br_arp_on;
volatile uint32_t br_gf_kick;
volatile uint32_t br_punch_effect, br_punch_loop_len;
br_arp_keys_t br_arp_keys[BR_SYNTH_PARTS];
volatile br_arp_rec_t br_arp_rec[BR_ARP_REC_SLOTS];
volatile uint32_t br_arp_rec_write, br_arp_rec_read;
br_master_t br_master = {0, {64, 0, 0, 0, 0, BR_HEAT_BIT, 0}, BR_SELECT_MASTER_FILTER, BR_COMP_DEFAULTS};
br_compressor_config_t br_source_compressor = BR_COMP_DEFAULTS;
uint32_t br_source_compressor_revision = 0xffffffffu;
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
int32_t br_comp_key[BR_PARTS][BR_BLOCK] __attribute__((section(".pool")));
#else
int32_t br_comp_key[BR_PARTS][BR_BLOCK];
#endif

#include "instrument_presets.c"
#include "instrument_audio.c"
#include "instrument_fx.c"
