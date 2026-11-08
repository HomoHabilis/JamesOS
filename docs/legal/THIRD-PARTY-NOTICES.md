# Third-party notices for JamesOS

JamesOS firmware code is licensed under **GPL-3.0-only** (see `LICENSE`). The following components keep their
own terms and copyright notices.

## Platform layer (GPL-3.0-only)
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, with the changes of SLOOP (isod89):
`firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, the platform files in `firmware/src/`, and `tools/`.

## Plaits, Braids and stmlib (MIT)
Synthesis code in `firmware/plaits/upstream/` and `firmware/braids/upstream/`.
Copyright 2012, 2013, 2014, 2015, 2016, 2017 and 2021 Emilie Gillet. Released under the MIT licence; the
licence text is in `firmware/plaits/upstream/MIT-LICENSE.txt` and `firmware/braids/upstream/MIT-LICENSE.txt`, and the
stmlib support library's is in `firmware/plaits/upstream/stmlib/LICENSE`. These notices must accompany copies
of the relevant code; retain any component-specific notices in that upstream licence file as well.
JamesOS is not made, supported or endorsed by Mutable Instruments or Emilie Gillet. The names Plaits and
Braids identify the origin of the code (nominative use) and are not claimed.

## Built-in drum kits (CC0 1.0)
Generator `tools/synth_drum_kits.py` and encoded data in `assets/drums/generated/`.
Original work, computed from oscillator and noise equations. No recording of any drum machine or sample
pack is used. Dedicated to the public domain under CC0 1.0 Universal. The folder names 808 and 909 and the word
"style" in the documentation describe the kind of sound only.

## Chip vendor files in the update package (Apache-2.0)
The update package contains three unmodified files from Zhuhai JieLi Technology's
[AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/tags), tag
`AC79NN_SDK_V1.2.1_2023-12-13` (commit `d179b4484759423312073f5fbb232501aa491047`).
Their bytes match that tag and the package validator's pins. Paths below are relative to `cpu/wl82/tools/`.

| File | SHA-256 |
| --- | --- |
| `uboot.boot` | `4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4` |
| `cfg_tool.bin` | `276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b` |
| `cfg/eq_cfg_hw.bin` | `41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d` |

The [Apache 2.0 licence from the exact SDK tag](APACHE-2.0-JIELI.txt) must accompany the release. The vendor tree
has no separate NOTICE file at the SDK root or in the directories containing these files. The SDK checkout
is a separate build input.

## Original manufacturer firmware

M-VAVE's stock firmware is separate from JamesOS firmware and source. Do not distribute manufacturer firmware
unless the applicable rights for the specific files have been established.

## Trademarks
M-VAVE, FM-1, Roland, TR-808, TR-909, Mutable Instruments, Plaits, Braids and any other names are the property of
their owners and are used only to identify what this software works with or where code came from. JamesOS is an
independent, unofficial firmware.
