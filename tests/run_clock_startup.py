#!/usr/bin/env python3
"""Exercise the real TIMER4 initializer and LCD sequence with mocked I/O."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parent
firmware = root.parent / "firmware"


def source(path):
    text = path.read_text()
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


def body(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", text)
    if not match:
        raise AssertionError("missing function definition: " + name)
    start = match.end() - 1
    depth = 0
    for pos in range(start, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:pos]
    raise AssertionError("unclosed function: " + name)


platform = source(firmware / "src/platform.c")
startup = body(platform, "fm1_cstart")
guard = re.search(
    r"if\s*\(\s*!\s*fm1_cpu1_early_hold_before_bss\s*\(\s*\)\s*\|\|\s*"
    r"!\s*fm1_cpu1_worker_hw_reset_held\s*\(\s*\)\s*\)\s*"
    r"for\s*\(\s*;\s*;\s*\)\s*\{\s*\}", startup)
assert guard, "TIMER4 must not be repaired without a confirmed held worker"
time_init = startup.index("fm1_time_init();")
assert startup.count("fm1_time_init();") == 1
assert guard.end() < time_init < startup.index("fm1_p33_checked_read(")
assert time_init < startup.index("fm1_wdt_arm_checked(")
assert time_init < startup.index("recovery_main();") < startup.index("lcd_init();")
assert time_init < startup.index("fm1_lcd_enable_backlight();") < startup.index("recovery_key()")
assert startup.index("recovery_key()") < startup.index("ota_boot_cleanup();")

# The early hold and its explicitly called helpers must remain independent of
# TIMER4; this is a scoped source check, not target control-flow emulation.
early_hw = source(firmware / "hal/fm1_cpu1_early_hw.h")
early_pc = source(firmware / "hal/fm1_cpu1_early_pc_window.h")
worker_hw = source(firmware / "hal/fm1_cpu1_worker_target_hw.h")
for text, name in (
    (platform, "fm1_cpu1_early_hold_before_bss"),
    (platform, "fm1_cpu1_early_integrity"),
    (early_hw, "fm1_cpu1_early_hw_read_control"),
    (early_hw, "fm1_cpu1_early_hw_write_control"),
    (early_hw, "fm1_cpu1_early_hw_barrier"),
    (early_pc, "fm1_cpu1_early_pc_windows_match"),
    (worker_hw, "fm1_cpu1_worker_hw_reset_held"),
):
    helper = body(text, name)
    assert not re.search(r"\bfm1_(?:ticks|delay_\w+|time_init)\s*\(|\bFM1_T4_", helper), name

recovery = body(source(firmware / "src/recovery.c"), "recovery_main")
assert "lcd_init();" in recovery
loader = body(source(firmware / "loader/loader.c"), "ldr_main")
assert loader.count("fm1_time_init();") == 1
assert loader.index("fm1_time_init();") < loader.index("usb_start();")
print("Startup hold, TIMER4, P33, recovery and loader source order: passed")

with tempfile.TemporaryDirectory(prefix="fm1-clock-startup-") as tmp:
    for name, defines in (("test_fm1_time_init", []),
                          ("test_fm1_time_init", ["-DFELUCCA_LOADER=1"]),
                          ("test_lcd_crystal_wait", [])):
        target = Path(tmp) / name
        subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", *defines, str(root / (name + ".c")),
                        "-o", str(target)], check=True)
        subprocess.run([str(target)], check=True)
