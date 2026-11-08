#!/usr/bin/env python3
"""Check preprocessed protocol paths and capture symbols in both device builds.

This is a build inspection, not a substitute for protocol or hardware tests.
"""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1] / 'build'
for directory, mode, enabled in [('pattern-device-candidate', 'development', True),
                                 ('pattern-device-candidate-distribution', 'distribution', False)]:
    out = root / directory
    settings = json.loads((out / 'build-settings.json').read_text())
    assert settings['diagnostics_configuration'] == mode
    assert settings['fault_ram_code_capture_read'] is enabled
    assert settings['optional_app_p33_and_work_read_commands'] is enabled
    assert settings['diagnostics_free'] is (not enabled)
    assert 'operational CPU monitor' in settings['diagnostics_free_scope']
    source = (out / 'dinghy.i').read_text()
    symbols = (out / 'dinghy.symbols').read_text()
    rescue = source[source.index('static int fm1_rescue_command('):source.index('static int fm1_app_command(')]
    for command in ('0xa2u', '0xa0u', '0x6fu', '0x7du'):
        assert f'cmd == {command}' in rescue, command
    for command in ('0x6au', '0xaeu', '0x99u', '0x89u'):
        assert (f'cmd == {command}' in rescue) is enabled, command
    # Separate rescue dispatch from the app dispatcher; both remain present.
    assert ('if (cmd == 0xaeu) return fm1_power_read_command' in source) is enabled
    assert 'if (cmd == 0xa0u && n == 3u)' in source  # rescue settings read
    assert 'cmd == 0x7Cu && n == 4u' in source  # guarded fault reboot
    assert 'cmd == 0x7du && n == 3u' in source  # retained fault bundle
    assert 'cmd == 0xA0u && !n' in source  # session protection
    assert 'cmd == 0x62u && !n' in source  # session/protocol status
    assert ('cmd == 0x60u && !n' in source) is enabled
    assert ('cmd == 0x61u && n == 4u' in source) is enabled
    assert ('fm1_foreground_journal_store_poll(fm1_ticks())' in source) is enabled
    assert 'cmd == 0x63u && n == 3u' in source  # session read
    assert 'cmd == 0x64u && n >= 3u' in source  # session upload
    assert 'cmd == 0x65u && n == 4u' in source  # guarded restore
    assert 'cmd == 0x68u && !n' in source  # legacy-session status
    assert ('else if (cmd == 0xaeu)' in source) is enabled
    assert ('else if (cmd == 0x9bu' in source) is enabled
    assert ('else if (cmd == 0x99u' in source) is enabled
    assert ('else if (cmd == 0x66u' in source) is enabled
    assert ('else if (cmd == 0x9Du' in source) is enabled
    assert ('else if (cmd == 0x9Eu' in source) is enabled
    assert ('else if (cmd == 0x69u' in source) is enabled
    assert ('else if (cmd == 0x6Cu' in source) is enabled
    assert ('else if (cmd == 0x8Cu' in source) is enabled
    assert ('else if (cmd == 0x70u' in source) is enabled
    assert ('else if (cmd == 0x7Eu' in source) is enabled
    assert ('else if (cmd == 0x86u' in source) is enabled
    assert ('else if (cmd == 0x82u' in source) is enabled
    assert ('else if (cmd == 0xCCu' in source) is enabled
    for command in ('0x6Au', '0x6Du', '0x74u', '0x75u', '0x76u', '0x7Bu', '0x7Fu', '0x78u', '0x79u', '0x7Au', '0x87u', '0x89u', '0x94u', '0x96u', '0x81u', '0x77u', '0x88u', '0x8Au', '0x8Bu', '0x95u', '0x9Au', '0x91u', '0x92u', '0xb2u', '0xb3u', '0xC8u', '0xCAu', '0xCDu', '0xC9u'):
        assert (f'else if (cmd == {command}' in source) is enabled, command
    assert ('else if (cmd == 0x90u' in source) is enabled
    assert ('br_half_timing_store' in symbols) is enabled
    assert ('br_diagnostic_deadline' in symbols) is enabled
    assert ('br_project_diag_down' in symbols) is enabled
    assert ('br_diagnostic_profile' in symbols) is enabled
    assert ('br_diagnostic_detail' in symbols) is enabled
    assert ('br_diagnostic_synth_ticks' in symbols) is enabled
    for name in ('br_tail_stamp', 'br_gap_stamp', 'fm1_tail_detail', 'fm1_gap_detail',
                 'fm1_audio_cpu1_join_tick', 'fm1_audio_cpu1_join_valid',
                 'fm1_audio_tail_last_wait_to_ack', 'fm1_audio_tail_valid_halves'):
        assert (name in symbols) is enabled, name
    assert ('br_audio_source_cost_diagnostic_snapshot' in symbols) is enabled
    assert ('br_audio_source_diagnostic_snapshot' in symbols) is enabled
    for name in ('br_audio_late_drop_count', 'br_audio_partial_drop_count',
                 'fm1_audio_late_drops', 'fm1_audio_late_contained_faults',
                 'fm1_foreground_journal_store_timer',
                 'fm1_cpu1_worker_diag', 'fm1_power_fault_capture_owned',
                 'fm1_vpd_fault_capture_owned', 'br_fx_progress_get',
                 'br_fx_progress_bootstrap', 'br_fx_serial_mode',
                 'fm1_ram_image_crc_record'):
        assert (name in symbols) is enabled, name
    assert ('br_audio_source_job_timing_snapshot' in symbols) is enabled
    assert ('br_audio_source_stage_timing_snapshot' in symbols) is enabled
    assert ('br_engine_clock_diag_snapshot' in symbols) is enabled
    for name in ('br_clock_f8_processed', 'br_clock_f8_rejected', 'br_clock_f8_suspect',
                 'br_clock_fail_count', 'br_clock_last_failure', 'br_clock_interval_count',
                 'br_clock_interval_sum', 'br_clock_prev_f8_frame'):
        assert (name in symbols) is enabled, name
    assert ('br_gr_stats' in symbols) is enabled
    assert ('br_gr_cost_ticks' in symbols) is not enabled
    for name in ('br_diagnostic_source_cost', 'br_diagnostic_part_synth_ticks'):
        assert name in symbols, name  # scheduler inputs, not disposable counters
    for name in ('br_audio_work_ticks', 'br_audio_work_jobs', 'br_audio_split_blocks',
                 'br_audio_split_tracks', 'br_fx_master_work_ticks', 'br_fx_gr_work_ticks'):
        assert (name in symbols) is enabled, name
    assert ('fm1_fault_dual_code_capture_owned' in symbols) is enabled
    assert ('fm1_fault_ram_code_capture_owned' in symbols) is enabled
    assert ('fm1_fault_dual_code_record\n' in symbols) is enabled
    assert ('fm1_fault_dual_code_reserved\n' in symbols) is not enabled
    assert ('fm1_fault_ram_code_record\n' in symbols) is enabled
    assert ('fm1_fault_ram_code_reserved\n' in symbols) is not enabled
    assert ('worker_smoke_callback' in symbols) is enabled
    assert ('worker_smoke_result' in symbols) is enabled
    for name in ('fm1_fault_popframe_diag', 'fm1_fault_c1_timeout_record',
                 'fm1_fault_c0_stack_window_record', 'fm1_fault_c1_context_record'):
        assert (name in symbols) is enabled, name
    assert 'worker_dispatch_target' in symbols
def retained_sections(directory):
    sections = {}
    for line in (root / directory / 'dinghy.symbols').read_text().splitlines():
        fields = line.split()
        if len(fields) >= 6 and fields[1:3] == ['l', 'd'] and fields[-1].startswith('.noinit'):
            sections[fields[-1]] = int(fields[0], 16)
    return sections
development = retained_sections('pattern-device-candidate')
distribution = retained_sections('pattern-device-candidate-distribution')
assert development == distribution, (development, distribution)
print('PASS: built command selection, rescue/protection paths, and capture symbols')
