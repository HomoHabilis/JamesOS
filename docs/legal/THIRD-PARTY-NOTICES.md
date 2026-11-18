# Third-party notices for JamesOS

JamesOS firmware code is licensed under **GPL-3.0-only** (see `LICENSE`). The following components keep their
own terms and copyright notices.

This source tree builds two products: **JamesOS** for the M-VAVE FM-1 and **JamesOS-K** for the M-VAVE SMK-37 Pro.
Unless a section says otherwise, it applies to both. Sections marked *JamesOS-K only* or *HD builds only* apply only
to firmware built for that product or configuration.

## Platform layer (GPL-3.0-only)
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, with the changes of SLOOP (isod89):
`firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, the platform files in `firmware/src/`, and `tools/`.
Upstream: [Felucca](https://github.com/hugelton/Felucca) and [SLOOP](https://github.com/isod89/sloop-fm1), both
published under the GNU GPL version 3.

## Dinghy (GPL-3.0-only)
JamesOS is built on [Dinghy](https://github.com/zvenson/dinghy) by zvenson, which carries the platform layer above;
the application file `firmware/src/dinghy.c` keeps Dinghy's name. Dinghy is published under GPL-3.0-only; its
`LICENSE` is the same GNU GPL version 3 text as this repository's.

## SMK-37 Pro port (GPL-3.0-only), JamesOS-K only
The SMK-37 Pro input layer and display/pad transport come from M. Geisler's (genkigenki/makkuro) Felucca SMK-37
port: branch `smk37` of [genkigenki/Felucca](https://github.com/genkigenki/Felucca) at commit
`b67312cd25bd01b097ed69388b83e86bd8449365`, offered to Felucca as
[pull request 237](https://github.com/hugelton/Felucca/pull/237). Those files are marked GPL-3.0-only, and that
repository's `LICENSE` is the same GNU GPL version 3 text as this repository's.

| File | Origin | Copyright as stated in the file |
| --- | --- | --- |
| `firmware/hal/smk37_input.h` | modified from the port's `firmware/hal/smk_input.h` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; SMK-37 Pro port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) |
| `firmware/hal/fm1_lcd_hw.h` (SMK-37 Pro LCD and RGB-pad transport) | based on the port's `firmware/hal/fm1_lcd_hw.h` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments |
| `firmware/src/lcd.c` (pad LED transport) | derived from the port's `firmware/src/lcd.c` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments |

The hardware notes at [genkigenki/fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37), referred to in
`smk37_input.h`, are a separate guide under CC BY-NC 4.0 that, by its own statement, contains no firmware code. No
text from it is reproduced here.

## Plaits, Braids and stmlib (MIT)
Synthesis code in `firmware/plaits/upstream/` and `firmware/braids/upstream/`.
Copyright 2012, 2013, 2014, 2015, 2016, 2017, 2019 and 2021 Emilie Gillet. Released under the MIT licence; the
licence text is in `firmware/plaits/upstream/MIT-LICENSE.txt` and `firmware/braids/upstream/MIT-LICENSE.txt`, and the
stmlib support library's is in `firmware/plaits/upstream/stmlib/LICENSE`. These notices must accompany copies
of the relevant code; retain any component-specific notices in that upstream licence file as well.
Files in those directories that carry an `SPDX-License-Identifier: GPL-3.0-only` line are JamesOS additions.
The adapter files in `firmware/plaits/` and `firmware/braids/` (outside `upstream/`) that carry an
`SPDX-License-Identifier: MIT` line are under the MIT licence.
JamesOS is not made, supported or endorsed by Mutable Instruments or Emilie Gillet. The names Plaits and
Braids identify the origin of the code (nominative use) and are not claimed.

## Plaits symbols (CC BY-SA 3.0)
The sixteen Plaits model symbols in `firmware/src/instrument_plaits_icons.h` are adapted from the Mutable Instruments
documentation ([pichenettes/mutable-instruments-documentation](https://github.com/pichenettes/mutable-instruments-documentation),
licensed CC BY-SA 3.0): resized and converted to monochrome for the display, and redrawn as anti-aliased line art in
HD builds. Licence: https://creativecommons.org/licenses/by-sa/3.0/.

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
FM-1 and SMK-37 Pro packages both contain them.

| File | SHA-256 |
| --- | --- |
| `uboot.boot` | `4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4` |
| `cfg_tool.bin` | `276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b` |
| `cfg/eq_cfg_hw.bin` | `41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d` |

The [Apache 2.0 licence from the exact SDK tag](APACHE-2.0-JIELI.txt) must accompany the release. The vendor tree
has no separate NOTICE file at the SDK root or in the directories containing these files. The SDK checkout
is a separate build input.

## HD interface typeface (SIL OFL 1.1), HD builds only
Builds made with `BR_UI_HD=1` draw text from glyph bitmaps in `firmware/src/james_font_hd.h`, generated by
`tools/gen_hd_font.py` from Chakra Petch SemiBold 1.000 (font file and OFL.txt in `tools/fonts/chakrapetch/`).
Copyright 2018 The Chakra Petch Project Authors. Released under the
SIL Open Font License 1.1; the source details and the full licence text are in
[HD-FONT-NOTICE.md](HD-FONT-NOTICE.md), which must accompany any release built with the HD interface. HD is the
default of `tools/build.py`. Standard-interface builds (`BR_UI_HD=0`, such as release 1.0a) contain none of this data; that interface has been
removed and can no longer be built.

## M-VAVE FM-1 boot and update resources

M-VAVE's FM1 Open [third-party notice](MVAVE-FM1-OPEN-THIRD-PARTY.md) identifies its boot/update resources and
V15 recovery image as Apache-2.0 material. The combined FM-1 package uses the manufacturer's encoded
`uboot.boot`, its `boot-config.bin` modified for the clock profile, and the OTA resource from its V15
recovery image. [MVAVE-BOOT-PROVENANCE.md](MVAVE-BOOT-PROVENANCE.md) records the exact revision, hashes and
five configuration-byte modifications. Include the manufacturer notice and the Apache-2.0 licence with
the distribution and retain applicable attribution and modification notices. Other components retain their
own licences; this does not establish rights for arbitrary stock firmware or other device models.

## Trademarks
M-VAVE, FM-1, SMK-37 Pro, Roland, TR-808, TR-909, Mutable Instruments, Plaits, Braids and any other names are the
property of their owners and are used only to identify what this software works with or where code came from.
JamesOS and JamesOS-K are independent, unofficial firmware.
