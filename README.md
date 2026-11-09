# JamesOS

<p align="center">
  <img src="assets/jamesos-start.png" alt="JamesOS startup graphic" width="200">
</p>

JamesOS is community firmware for the M-VAVE FM-1, built on Dinghy and the Felucca platform with SLOOP contributions. It adds synth engines, drum kits, sequencing and performance controls. The project is independent of the hardware manufacturer.

JamesOS is a Gjelly project by Aaron Grando (a.k.a. James Os).

<table>
  <tr>
    <td align="center"><img src="docs/images/home.png" alt="JamesOS sound controls" width="240"><br>Sound controls</td>
    <td align="center"><img src="docs/images/lfo.png" alt="JamesOS LFO controls" width="240"><br>LFO</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/mixer.png" alt="JamesOS track mixer" width="240"><br>Mixer</td>
    <td align="center"><img src="docs/images/drum-sequence.png" alt="JamesOS drum sequencer with programmed steps" width="240"><br>Drum sequencer</td>
  </tr>
</table>

Screens shown above were rendered from the 1.0a firmware source in the host emulator.

Release candidates are identified by source tags and matching firmware packages. Hardware compatibility, update and recovery coverage are documented for each candidate.

The existing **1.0a** release record describes build 2014. Build 2103 is a newer release candidate with an updater that accepts compatible official firmware packages, including the tested return to V15.

The `releases/` directory contains two build 2103 packages:

- `JamesOS-1.0a-build-2103.fwsc` is the standard application update package, tested from JamesOS 2014.
- `JamesOS-1.0a-build-2103-from-v15.fwsc` includes the boot configuration required for 396 MHz operation and the application in a single package. It was tested from official V15 on one FM-1. Its official update loader and boot-area changes must be compatible with the installed firmware; other source firmware versions and hardware revisions have not been qualified.

Both packages identify as `FM-1_9262103`. Returning from build 2103 to official V15 was tested on the same device. Firmware changes can overwrite saved data; interrupted updates and backup restoration have not been qualified. The combined package incorporates material derived from M-VAVE's V14 firmware; confirm its redistribution terms before distributing it.

To reproduce the tested combined package, provide a separately obtained official V14 firmware package (SHA-256 `7e7b6d260163cdf9d716f7fab44f19be600edeffcd390de74370b83c917fd0c6`):

```sh
python3 tools/build_v15_install_package.py --app releases/JamesOS-1.0a-build-2103.fwsc --v14 /path/to/official-v14.fwsc --output /tmp/jamesos-2103-from-v15.fwsc
```

The output SHA-256 is `a54bb59fcb4e2a932bb1a4bb613971a1174e50c828a7bc69e94d72a23ae813ad`. The standard package SHA-256 is `267b9b58a47e60cd6675ffb494adfdbeacd1a76e8e36a678522a8b875d25f89e`.

## Source layout

- `firmware/` contains the firmware, loader, hardware layer and synthesis libraries.
- `assets/` contains inputs required to regenerate firmware data.
- `tools/` contains build, development and recovery tools.
- `tests/` contains host-side regression tests and their fixtures.

## Build and test

The device build requires the JieLi Linux toolchain and the AC79 SDK tag pinned by `tools/build.py`; see `tools/get_toolchain.sh`. Set `JIELI_TOOLCHAIN` to the toolchain directory and `AC79_SDK` to the SDK checkout. On macOS, Docker is required. The following commands select development and distribution configurations. Build the development configuration with:

```sh
python3 tools/build.py --pattern-device-candidate --app-opt Oz --product FM-1_9262013 --diagnostics development --sdk "$AC79_SDK"
```

Build the distribution configuration with:

```sh
python3 tools/build.py --pattern-device-candidate --app-opt Oz --product FM-1_9262103 --diagnostics distribution --sdk "$AC79_SDK"
```

The distribution configuration excludes development-only controls, telemetry, profiling and optional crash sidecar capture. It retains operational timing, safety, fault and recovery paths and rejects optional profiling and test modes. Build output includes `build-settings.json` identifying the selected configuration. See `docs/DIAGNOSTICS.md` for details.

Native host tests use a C/C++ compiler and Python. For example, `python3 tests/run_global_timing.py` builds the host DSP objects and runs a timing regression without accessing hardware. Generated outputs belong in `build/`.

Installing unofficial firmware can risk project data or make the device unresponsive. Hardware compatibility, upgrade and recovery paths remain under active validation.

## Licences and credits

JamesOS firmware code is GPL-3.0-only; see `LICENSE`. The platform derives from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita and Hügelton Instruments, with [SLOOP](https://github.com/isod89/sloop-fm1) contributions, and [Dinghy](https://github.com/zvenson/dinghy) by zvenson. Plaits, Braids and stmlib retain their MIT licences in their upstream directories. Component notices and the JieLi SDK licence are in `docs/legal/`.
