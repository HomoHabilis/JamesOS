#define DINGHY_HOST 1
#define DINGHY_SMK_PREVIEW 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <string.h>

int main(void)
{
    uint32_t held = 0u;
    uint8_t velocity[27]; memset(velocity, 100, sizeof velocity);
    br_app_init();
    br_ui.part = 0u; br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0u;
    br_smk_seq_keys(&held, 3u, 3u, 1u, velocity);
    assert(held == 3u && br_ui.held_note[37] == 54u && br_ui.held_note[38] == 55u);
    br_key_up(0u);
    assert(br_ui.held_note[37] == 54u);
    br_smk_seq_keys(&held, 2u, 0u, 1u, velocity);
    assert(!br_ui.held_note[37] && br_ui.held_note[38] == 55u);
    br_smk_seq_keys(&held, 2u, 2u, 1u, velocity);
    assert(br_ui.held_note[38] == 55u);
    br_smk_seq_keys(&held, 0u, 0u, 0u, velocity);
    assert(!held && !br_ui.held_note[38]);
    br_smk_pedal_set(1u);
    br_smk_seq_keys(&held, 1u, 1u, 1u, velocity);
    br_smk_seq_keys(&held, 0u, 0u, 1u, velocity);
    assert(!br_ui.held_note[37] && br_ui.sustain_note[37] == 54u);
    br_smk_pedal_set(0u);
    assert(!br_ui.sustain_note[37]);
    /* Compact note storage must retain the full MIDI range, including 127. */
    br_play_key(37u, 1, 0u, 127u, 100u);
    assert(br_ui.held_note[37] == 128u);
    br_smk_pedal_set(1u); br_key_up(37u);
    assert(br_ui.sustain_note[37] == 128u);
    br_smk_pedal_set(0u);
    assert(!br_ui.sustain_note[37]);
    br_ui.page = BR_PAGE_SCALE; br_ui.part = 0u;
    br_sel.cursor[0] = 0u; br_smk_next_sound_category();
    assert(br_sel.cursor[0] == 32u && br_sel.active);
    br_sel_close();
    br_smk_pedal_set(1u); br_hold_on = 1u;
    br_smk_seq_keys(&held, 1u, 1u, 1u, velocity);
    br_smk_seq_keys(&held, 0u, 0u, 1u, velocity);
    br_hold_toggle(); /* Releasing HOLD while the pedal is down must sustain. */
    assert(br_ui.sustain_note[37]);
    br_smk_pedal_set(0u);
    assert(!br_ui.sustain_note[37]);
    return 0;
}
