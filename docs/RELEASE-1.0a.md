# JamesOS 1.0a alpha candidate

JamesOS is an independent firmware project by Aaron Grando (a.k.a. James Os) and Gjelly for the M-VAVE FM-1. This replacement 1.0a candidate is **under validation and not yet frozen**.

The distribution test build is `FM-1_9262014` and the matching development test build is `FM-1_9262013`; both use update loader `ota-FM-1_904`. Firmware switching can overwrite another firmware's saved data. The candidate identifies incompatible project slots and permits a confirmed save to replace them. It does not convert projects from other firmware.

| Test package | SHA-256 |
| --- | --- |
| Distribution | `8e2efc76bf7cf18e0cd10df3a053e5b3fadbfa0a74aa88543b9d832356e5556b` |
| Development | `ca964cdbcff5d3f4f0a1872d380b50e2ccd7755f445eb16c9861de965119a398` |

Both packages built with the pinned SDK and toolchain and passed independent package validation. Host tests exercised foreign working data, replacement of incompatible named slots, valid-survivor preservation, read failures and simulated interrupted writes. Distribution build 2014 installed and restarted on the tested FM-1; the on-device foreign-data test remains in progress. Development build 2013 has not been tested on hardware. These checks do not qualify other devices or power-interrupted firmware updates.

The prior 2012 candidate was tested on one FM-1 with flash ID `0x856014`, including save and reload, invalid-record and capacity checks, physical rescue entry, a powered interrupted transfer and selected USB MIDI checks. Those checks are not a substitute for testing the changed firmware. Hardware with flash ID `0xB36014`, physical interrupted saves, DIN MIDI and an independent MIDI clock remain unverified. The long playback soak and extended idle-clock test were deferred. Returning to M-Vave firmware via its Workbench remains unqualified.

Component notices are in `legal/THIRD-PARTY-NOTICES.md`. Build configuration and toolchain requirements are in the project README.
