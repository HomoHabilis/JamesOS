/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SMK37_PANEL_LOGIC_H
#define SMK37_PANEL_LOGIC_H
#include <stdint.h>
/* Pure decisions of the SMK-37 panel (host-tested by tests/test_smk_panel_logic.c). */

/* NOTE REPEAT as a tap. On the drum track it toggles the RETRIGGER page: a tap opens it, a tap
 * again returns to the page it came from (any page button leaves it as usual). On synth tracks it is
 * the punch shift key, as before. blocked: a step press is pending (drum track only; SAVE open never
 * reaches the panel code). retrig_shown: the page now shown is RETRIGGER. opened: NOTE REPEAT opened it. */
enum { SMK_REPEAT_NONE, SMK_REPEAT_PUNCH, SMK_REPEAT_OPEN_RETRIG, SMK_REPEAT_RETURN };
static inline uint32_t smk_repeat_decide(uint32_t on_drum, uint32_t retrig_shown, uint32_t opened,
                                         uint32_t blocked)
{
    if (!on_drum) return SMK_REPEAT_PUNCH;
    if (blocked) return SMK_REPEAT_NONE;
    if (!retrig_shown) return SMK_REPEAT_OPEN_RETRIG;
    return opened ? SMK_REPEAT_RETURN : SMK_REPEAT_NONE;
}

/* FADER BANK: it switches the bank and opens that bank's real mixer page (bank 0: MIXER, bank 1: KIT
 * LEVEL), which stays until a page button leaves it. With a step held it only switches the bank and says
 * which one. It never shows the brief peek. */
enum { SMK_BANK_PAGE, SMK_BANK_MESSAGE };
static inline uint32_t smk_bank_press_decide(uint32_t step_locked)
{
    return step_locked ? SMK_BANK_MESSAGE : SMK_BANK_PAGE;
}

/* A fader moved: the brief 1.6 s peek of the bank's mixer shows only off the mixer pages (MIXER, KIT
 * LEVEL), and not while a step is held. On a mixer page the move just updates the page. */
static inline uint32_t smk_fader_peek_wanted(uint32_t step_locked, uint32_t on_mixer_page)
{
    return !step_locked && !on_mixer_page;
}
#endif
