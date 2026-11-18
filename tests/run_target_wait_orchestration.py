#!/usr/bin/env python3
"""Exercise the target's extracted join/late-classification control flow."""
from pathlib import Path
import host_target
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = host_target.select_product((root / "firmware/src/instrument_audio.c").read_text())  # the selected product's lines (BR_HOST_TARGET)
# SMK-37 Pro (JamesOS-K) prefixes these conditions with BR_POLY_STANDARD_GRAPH, which
# instrument_audio.c defines as the constant 1 without polyphony: drop the "1 &&".
if not host_target.known_macros()['BR_CAP_POLY']:
    source = source.replace("BR_POLY_STANDARD_GRAPH && ", "")


def through_brace(start, brace):
    depth = 0
    for pos in range(brace, len(source)):
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return pos + 1
    raise AssertionError("unterminated source block")


def function(prefix):
    start = source.index(prefix)
    end = through_brace(start, source.index("{", start))
    return source[start:end]


def block(prefix, offset=0, include_else=False):
    start = source.index(prefix, offset)
    end = through_brace(start, source.index("{", start))
    if include_else:
        assert source[end:].lstrip().startswith("else {")
        end = through_brace(end, source.index("{", end))
    return source[start:end], end


helpers = ["static uint32_t br_audio_source_late_join_valid(",
           "static uint32_t br_audio_source_c1_late_read(",
           "static uint32_t br_audio_source_accept_complete_late(",
           "static uint32_t br_audio_source_complete_watch_valid(",
           "static void br_audio_source_drop_partial("]
selector_from = source.index("uint32_t join_timeout = br_audio_source_join_timeout_ticks();")
selector, position = block("if (BR_AUDIO_C1_LATE_OWNER_KNOWN && BR_AUDIO_FUSION_ONLY_BUILD &&",
                           selector_from, include_else=True)
# With BR_POLY_SUPPORT (every SMK-37 build, any polyphony build) instrument_audio.c redefines
# BR_AUDIO_C1_LATE_OWNER_KNOWN before the late-owner selectors (adding the standard-graph test)
# and restores it inside the extracted text. Take that redefinition from the source too, so the
# harness sees the selectors exactly as the selected configuration compiles them.
known = host_target.known_macros()
undef = source.find("#undef BR_AUDIO_C1_LATE_OWNER_KNOWN", selector_from,
                    source.index("if (BR_AUDIO_C1_LATE_OWNER_KNOWN && BR_AUDIO_FUSION_ONLY_BUILD &&", selector_from))
poly_redefinition = ""
if undef >= 0:
    lines, pos = [], undef
    while True:
        eol = source.index("\n", pos)
        lines.append(source[pos:eol])
        pos = eol + 1
        if not source[pos:].startswith("#define") and not lines[-1].endswith("\\"):
            break
    poly_redefinition = "\n".join(lines) + "\n"
assert bool(poly_redefinition) == bool(known["BR_POLY_SUPPORT"]), \
    f"late-owner redefinition {'missing' if known['BR_POLY_SUPPORT'] else 'unexpected'} (BR_POLY_SUPPORT={known['BR_POLY_SUPPORT']})"
assert "BR_POLY_STANDARD_GRAPH" in poly_redefinition or not known["BR_CAP_POLY"]
seal_start = source.index("if (br_audio_source_queue.fixed_plan_active &&", position)
seal_end = source.index("#endif", seal_start)
seal = "#if BR_AUDIO_FUSION_FIXED_PLAN\n" + source[seal_start:seal_end] + "#endif"
start = source.index("if (br_audio_source_publication_enabled) {", seal_end)
end = source.index("if (!br_audio_source_publication_enabled &&\n                c1_extended", start)
classification = source[start:end]
# A partial drop returns from the real void render function. This harness has
# an outcome-returning wrapper, so preserve that exit with a local label. All
# classification decisions and the actual partial-drop helper remain source.
assert classification.count("return;") == 1
classification = classification.replace("return;", "goto partial_exit;")
classifiers = [classification]
position = end
for prefix in ("if (!br_audio_source_publication_enabled &&\n                c1_extended",
               "if (cpu0_ok == BR_AUDIO_QUEUE_COMPLETE_LATE)"):
    selected, position = block(prefix, position)
    classifiers.append(selected)
start = source.index("if (\n#if BR_AUDIO_FUSION_FIXED_PLAN", position)
brace = source.index("{", start)
fault_condition = source[start:brace] + "{ ++fault_path; }"

with tempfile.TemporaryDirectory(prefix="fm1-target-wait-branch-") as temp:
    directory = Path(temp)
    (directory / "wait_branch_helpers.inc").write_text("\n\n".join(map(function, helpers)))
    (directory / "wait_branch_body.inc").write_text(
        poly_redefinition + "\n\n".join([selector, seal, *classifiers, fault_condition]) + "\n")
    for optimization in ("-O0", "-O2"):
        for block_size in (256, 128):
            binary = directory / (f"target_wait_{optimization[1:]}_{block_size}")
            subprocess.run(["cc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                            "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
                            f"-DTEST_BLOCK={block_size}u", "-I", str(directory),
                            f"-DBR_POLY_SUPPORT={known['BR_POLY_SUPPORT']}",
                            f"-DTEST_POLY_SCHEDULER={known['BR_CAP_POLY']}",
                            "-I", str(root / "firmware/hal"),
                            str(root / "tests/test_target_wait_orchestration.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
