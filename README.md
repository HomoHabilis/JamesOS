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

The **1.0a alpha** release now uses build 2104. It fixes a stuck note that could occur when a key was released and pressed again between input scans. On one FM-1, the standard 2104 update installed through loader 904 and restarted; the owner reported no stuck notes while rapidly repeating and alternating keys. This does not cover every playing pattern or hardware revision.

The `releases/` directory contains two build 2104 packages:

- `JamesOS-1.0a-build-2104.fwsc` is the application update package, tested from JamesOS 2103 on one FM-1.
- `JamesOS-1.0a-build-2104-from-v15.fwsc` includes the boot configuration needed for 396 MHz operation. It is derived from the 2103 V15 install package and the 2104 application; **the 2104 V15 installation path has not been tested on hardware**. Its official update loader and boot-area changes must be compatible with the installed firmware.

Both identify as `FM-1_9262104`. The preceding build 2103 was tested from official V15 and back to V15 on one FM-1. Firmware changes can overwrite saved data; interrupted updates and backup restoration have not been qualified. The combined package incorporates material derived from M-VAVE's V14 firmware; its redistribution terms remain to be confirmed.

To reproduce the 2104 combined package, provide a separately obtained official V14 firmware package (SHA-256 `7e7b6d260163cdf9d716f7fab44f19be600edeffcd390de74370b83c917fd0c6`):

```sh
python3 tools/build_v15_install_package.py --app releases/JamesOS-1.0a-build-2104.fwsc --v14 /path/to/official-v14.fwsc --output /tmp/jamesos-2104-from-v15.fwsc
```

The combined package SHA-256 is `edc080e0fcf0ebc56ff69bf52db2bb960006420c5904953a5884563e0b72fed6`; the standard package SHA-256 is `2257a08ad27793f07d204cc4b173c6e821408d0f4e65f556b3a1e4544bc76f58`.

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
python3 tools/build.py --pattern-device-candidate --app-opt Oz --product FM-1_9262104 --diagnostics distribution --sdk "$AC79_SDK"
```

The distribution configuration excludes development-only controls, telemetry, profiling and optional crash sidecar capture. It retains operational timing, safety, fault and recovery paths and rejects optional profiling and test modes. Build output includes `build-settings.json` identifying the selected configuration. See `docs/DIAGNOSTICS.md` for details.

Native host tests use a C/C++ compiler and Python. For example, `python3 tests/run_global_timing.py` builds the host DSP objects and runs a timing regression without accessing hardware. Generated outputs belong in `build/`.

Installing unofficial firmware can risk project data or make the device unresponsive. Hardware compatibility, upgrade and recovery paths remain under active validation.

## Licences and credits

JamesOS firmware code is GPL-3.0-only; see `LICENSE`. The platform derives from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita and Hügelton Instruments, with [SLOOP](https://github.com/isod89/sloop-fm1) contributions, and [Dinghy](https://github.com/zvenson/dinghy) by zvenson. Plaits, Braids and stmlib retain their MIT licences in their upstream directories. Component notices and the JieLi SDK licence are in `docs/legal/`.
