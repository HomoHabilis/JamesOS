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

The **1.0a alpha** release now uses build 2105. It identifies the USB device as `FM-1` to improve compatibility with external update and recovery tools. Build 2105 retains the rapid key-release fix from 2104.

The `releases/` directory contains two build 2105 packages:

- `JamesOS-1.0a-build-2105.fwsc` is the application update package. It was installed from JamesOS 2104 and restarted on one FM-1.
- `JamesOS-1.0a-build-2105-from-v15.fwsc` includes the boot configuration needed for 396 MHz operation. It combines the boot area and official loader tested with 2104 from V15 with the installed 2105 application. **This exact combined 2105 package has not been tested on hardware.**

Both identify as `FM-1_9262105`. The 2104 browser installation from V15 was tested on one FM-1 with flash ID `0x856014`; its boot area matched the JamesOS package after installation. Other flash hardware and interrupted updates remain unverified. Switching firmware can overwrite saved data. The combined package incorporates material derived from M-VAVE's V14 firmware; its redistribution terms remain to be confirmed.

On a Mac that previously recognized the device as JamesOS, the MIDI name may remain cached until updated in Audio MIDI Setup. M-Flasher detected the device after this adjustment. A tested M-Flasher restart entered USB rescue rather than normal firmware; a power cycle restored normal operation.

To reproduce the 2105 combined package, provide a separately obtained official V14 firmware package (SHA-256 `7e7b6d260163cdf9d716f7fab44f19be600edeffcd390de74370b83c917fd0c6`):

```sh
python3 tools/build_v15_install_package.py --app releases/JamesOS-1.0a-build-2105.fwsc --v14 /path/to/official-v14.fwsc --output /tmp/jamesos-2105-from-v15.fwsc
```

The combined package SHA-256 is `fa6fb07ecfa0ae92a34ddd3c09b09b37308cf5f41384f4c14dd6535941a334e1`; the standard package SHA-256 is `320ea899311024ef558f12f7b61ac0aaa9bb63024a292135202e017089c46fb4`.

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
python3 tools/build.py --pattern-device-candidate --app-opt Oz --product FM-1_9262105 --diagnostics distribution --sdk "$AC79_SDK"
```

The distribution configuration excludes development-only controls, telemetry, profiling and optional crash sidecar capture. It retains operational timing, safety, fault and recovery paths and rejects optional profiling and test modes. Build output includes `build-settings.json` identifying the selected configuration. See `docs/DIAGNOSTICS.md` for details.

Native host tests use a C/C++ compiler and Python. For example, `python3 tests/run_global_timing.py` builds the host DSP objects and runs a timing regression without accessing hardware. Generated outputs belong in `build/`.

Installing unofficial firmware can risk project data or make the device unresponsive. Hardware compatibility, upgrade and recovery paths remain under active validation.

## Licences and credits

JamesOS firmware code is GPL-3.0-only; see `LICENSE`. The platform derives from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita and Hügelton Instruments, with [SLOOP](https://github.com/isod89/sloop-fm1) contributions, and [Dinghy](https://github.com/zvenson/dinghy) by zvenson. Plaits, Braids and stmlib retain their MIT licences in their upstream directories. Component notices and the JieLi SDK licence are in `docs/legal/`.
