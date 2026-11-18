#!/usr/bin/env python3
"""A contained CPU1 fault: CPU0-only audio and SAVE (actual HAL functions, real park protocol).

Extracts the worker HAL functions from firmware/hal/fm1_cpu1_worker_target.c,
compiles them with tests/test_worker_fault_recovery.c and firmware/src/pattern_park_io.c
against a scripted timer, a simulated CPU1 (the real mailbox step) and a fake
reset register, at -O0 and -O2. Also checks that the source keeps the pieces of
the recovery that the extracted functions cannot show (dinghy.c, instrument_fx.c).
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/hal/fm1_cpu1_worker_target.c").read_text()


def body_from(text, start):
    brace = text.index("{", start)
    depth = 0
    for pos in range(brace, len(text)):
        depth += (text[pos] == "{") - (text[pos] == "}")
        if depth == 0:
            return text[brace:pos + 1]
    raise AssertionError("unterminated function")


def function(prefix, signature=None):
    start = source.index(prefix)
    return (signature or prefix) + "\n" + body_from(source, start + len(prefix))


parts = [
    function("uint32_t fm1_cpu1_worker_contained(void)"),
    function("int fm1_cpu1_worker_project_fence_acquire(void)"),
    function("int fm1_cpu1_worker_project_fence_release(void)"),
    function("uint32_t fm1_cpu1_worker_project_fence_held(void)"),
    function("uint32_t fm1_cpu1_worker_project_parked(void)"),
    function("uint32_t fm1_cpu1_worker_available(void)"),
    function("static uint32_t worker_wait_state(uint32_t wanted, uint32_t timeout_ticks)"),
    function("static uint32_t worker_submit_fixed(uint32_t task_id, void *context,",
             "static uint32_t worker_submit_fixed(uint32_t task_id, void *context, uint32_t *sequence_out)"),
    function("uint32_t fm1_cpu1_worker_wait(uint32_t sequence, uint32_t timeout_ticks,",
             "uint32_t fm1_cpu1_worker_wait(uint32_t sequence, uint32_t timeout_ticks, uint32_t *result_out)"),
    function("uint32_t fm1_cpu1_worker_park(uint32_t timeout_ticks)"),
    function("static uint32_t worker_resume_impl(uint32_t timeout_ticks)\n#else\n"
             "uint32_t fm1_cpu1_worker_resume(uint32_t timeout_ticks)\n#endif",
             "static uint32_t worker_resume_impl(uint32_t timeout_ticks)"),
    function("int fm1_cpu1_worker_project_authorize_resume(void)"),
    function("uint32_t fm1_cpu1_worker_project_resume(uint32_t timeout_ticks)"),
    function("uint32_t fm1_cpu1_worker_abort_hold_reset(void)"),
    function("uint32_t fm1_cpu1_worker_reset_held(void)"),
]
# The containment test must be free of sticky side effects (an audio IRQ may call it
# half-way through a foreground abort).
assert "uncontained_fault =" not in parts[0] and "note_reset" not in parts[0]

# Pieces outside the HAL that the recovery depends on (kept by text check).
dinghy = (root / "firmware/src/dinghy.c").read_text()
fx = (root / "firmware/src/instrument_fx.c").read_text()
audio = (root / "firmware/src/instrument_audio.c").read_text()
app = dinghy[dinghy.index("static uint32_t app_audio(int32_t *out, uint32_t n)"):]
app = app[:app.index("\nstatic void br_app_init(void)")]
assert "if (!fm1_cpu1_worker_contained()) {" in app            # silent only until held
assert "br_audio_source_worker_fault = 0u;" in app             # a contained fault costs one half
assert "|| fm1_cpu1_worker_fault" not in app                   # no latched mute on a held worker
for before in app.split("render_result = FM1_AUDIO_RENDER_CONTAINED_FAULT;")[:-1]:
    tail = before[-260:]                                       # the contained branch: never a latched mute
    assert "\n            br_diagnostic_muted = 1u;" not in tail, tail
transition = dinghy[dinghy.index("static int br_poly_foreground_transition(uint32_t part, uint32_t suspend)"):]
transition = transition[:transition.index("static int br_poly_before_update(void)")]
assert "worker_failed:" in transition and "br_quiescent_transaction_end();\n    return 0;" in transition
assert "fm1_cpu1_worker_project_parked()" in dinghy.split("static int br_ps_target_parked(void *ctx)")[1][:600]
assert "        br_fx_stream_wait;" in fx                    # the device's master-stream wait
wait = fx[fx.index("static __attribute__((noinline)) uint32_t br_fx_stream_wait("):]
assert "br_audio_stream_wait_ticks(br_pipeline_source_deadline_valid" in wait[:900]
assert "if (br_audio_source_worker_fault) {" in fx               # br_fx_process: this half only
assert "fm1_cpu1_worker_fault && !fm1_cpu1_worker_contained()" in audio

with tempfile.TemporaryDirectory(prefix="fm1-worker-fault-") as temp:
    directory = Path(temp)
    (directory / "worker_fault_extracted.inc").write_text("\n\n".join(p for p in parts if p) + "\n")
    for optimization in ("-O0", "-O2"):
        binary = directory / ("worker_fault_" + optimization[1:])
        subprocess.run(["cc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-but-set-variable",
                        "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
                        "-I", str(directory), "-I", str(root / "firmware/hal"),
                        "-I", str(root / "firmware/src"),
                        str(root / "tests/test_worker_fault_recovery.c"),
                        str(root / "firmware/src/pattern_park_io.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print("PASS: worker fault recovery (extracted HAL, -O0 and -O2)")
