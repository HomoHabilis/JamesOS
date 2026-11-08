#!/usr/bin/env python3
"""Exercise the target's extracted join/late-classification control flow."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/src/instrument_audio.c").read_text()


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
selector, position = block("if (BR_AUDIO_C1_LATE_OWNER_KNOWN && BR_AUDIO_FUSION_ONLY_BUILD &&",
                           source.index("uint32_t join_timeout = br_audio_source_join_timeout_ticks();"),
                           include_else=True)
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
        "\n\n".join([selector, seal, *classifiers, fault_condition]) + "\n")
    for optimization in ("-O0", "-O2"):
        for block_size in (256, 128):
            binary = directory / (f"target_wait_{optimization[1:]}_{block_size}")
            subprocess.run(["cc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                            "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
                            f"-DTEST_BLOCK={block_size}u", "-I", str(directory),
                            "-I", str(root / "firmware/hal"),
                            str(root / "tests/test_target_wait_orchestration.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
