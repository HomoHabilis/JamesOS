/* Forward a small number of clock ticks while a long canvas redraw is in
 * progress. Leave all other MIDI at the head for the ordinary input loop. */
static void br_ui_clock_service(void)
{
    unsigned n;
    if (!br_midi_clock_enabled || br_diagnostic_restore_in_progress ||
        br_engine_pipeline_uncontained_fault_get() || br_ps_tick_claimed ||
        br_pattern_irq_edits_blocked()) return;
    for (n = 0; n < 4u; ++n) {
        uint8_t st, d1, d2;
        uint32_t irq_state;
        if (!br_foreground_mutation_begin(&irq_state)) return;
        if (br_pattern_irq_edits_blocked() || mi_r == mi_w ||
            (uint8_t)(midi_in_q[mi_r % MQ] >> 8) != 0xf8u) {
            br_foreground_mutation_end(irq_state);
            return;
        }
        if (midi_get(&st, &d1, &d2)) br_midi(st, d1, d2);
        br_foreground_mutation_end(irq_state);
    }
}
