# JamesOS 1.0a alpha

**Current update: build 2105 (9 November 2026).** The USB product name is `FM-1` for broader tool compatibility. The application-only package installed from JamesOS 2104 and restarted on one FM-1. Its combined V15 package contains the same boot area and official loader as the hardware-tested 2104 combined package, and the same application as the installed 2105 package; the exact 2105 V15 package has not been tested on hardware. macOS may retain an old MIDI name. A tested M-Flasher restart entered USB rescue rather than the normal application; normal startup returned after a power cycle. See the README for hashes and limitations. The 2014 record below is historical.

JamesOS is an independent firmware project by Aaron Grando (a.k.a. James Os) and Gjelly for the M-VAVE FM-1. The owner froze the 1.0a alpha candidate on distribution build 2014 on 8 November 2026.

The distribution build is `FM-1_9262014` and the matching development build is `FM-1_9262013`; both use update loader `ota-FM-1_904`. Firmware switching can overwrite another firmware's saved data. JamesOS identifies incompatible project slots and permits a confirmed save to replace them. It does not convert projects from other firmware.

| Test package | SHA-256 |
| --- | --- |
| Distribution | `8e2efc76bf7cf18e0cd10df3a053e5b3fadbfa0a74aa88543b9d832356e5556b` |
| Development | `ca964cdbcff5d3f4f0a1872d380b50e2ccd7755f445eb16c9861de965119a398` |

Both packages built with the pinned SDK and toolchain, passed independent package validation and reproduced byte-for-byte from the matching source. Host tests exercised foreign working data, replacement of incompatible named slots, valid-survivor preservation, read failures and simulated interrupted writes. Distribution build 2014 installed and restarted on the tested FM-1. With captured incompatible data placed in test slot 4 and the working-project record, the project list appeared, slot 4 showed `USED DATA`, and another saved project loaded. The owner confirmed the overwrite prompt, saved over slot 4 and loaded it before and after a power cycle. A double-read flash comparison verified that all other named slots remained byte-for-byte unchanged. The FM-1 returned to 2014 with protection clear. A one-byte active-session difference was observed immediately after installation; before and after snapshots were preserved. Development build 2013 has not been tested on hardware. These checks do not qualify other devices or power-interrupted firmware updates.

The prior 2012 candidate was tested on one FM-1 with flash ID `0x856014`, including save and reload, invalid-record and capacity checks, physical rescue entry, a powered interrupted transfer and selected USB MIDI checks. Those checks are not a substitute for testing the changed firmware. Hardware with flash ID `0xB36014`, physical interrupted saves, DIN MIDI and an independent MIDI clock remain unverified. The long playback soak and extended idle-clock test were deferred. Returning to M-Vave firmware via its Workbench remains unqualified.

Component notices are in `legal/THIRD-PARTY-NOTICES.md`. Build configuration and toolchain requirements are in the project README.
