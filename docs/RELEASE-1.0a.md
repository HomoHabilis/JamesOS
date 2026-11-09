# JamesOS 1.0a alpha

JamesOS 1.0a is the alpha designation for the frozen firmware candidate identified as distribution build `FM-1_9262012`. It is an independent firmware project by Aaron Grando (a.k.a. James Os) and Gjelly for the M-VAVE FM-1.

The alpha designation identifies the existing candidate and does not change its firmware, loader or source package. The matching development build is `FM-1_9262011`; both use update loader `ota-FM-1_904`.

## Artifact identity

| Artifact | SHA-256 |
| --- | --- |
| Distribution package | `1bba4a934ec771c7eb7161a4c886725de94d6bd8f6bf70d753c7a6cc9785a74e` |
| Development package | `1467dfe43f39373f8eb6570bbf37373bb37d11316bef7e316663301344068811` |
| Original frozen source archive (not included in the publication bundle) | `bfc4c5703503a1cf34bf45360bfa9bd100a6d05ebea9609e313248fbf84369be` |

The frozen clean-history source commit is `97d831c6a8509f19470cd59fce26d97509898262`, with tree `6366a2ecb424c816f21e452db0bb291fb148707b`. This release record was added after the freeze and is not included in the original frozen source archive. The publication source archive includes updated documentation and a nonfunctional font-header comment change; its glyph data and firmware behavior remain unchanged. Check the accompanying checksum and provenance record. Build configuration and toolchain requirements are in the project README.

## Validation and limits

Build 2012 was tested on one FM-1 with flash ID `0x856014`. Checks included project save and reload, capacity and invalid-record rejection, physical rescue entry and exit, and a powered same-image update transfer that was interrupted and resumed. USB MIDI transport and selected incoming and outgoing routing were also checked. The owner accepted the partial outgoing drum comparison.

Power interruption during a partially written update or recovery and operation on the other supported flash ID `0xB36014` have not been verified. Some MIDI outputs, DIN MIDI and an independent MIDI clock remain untested. Physical interrupted-save testing and long-idle clock testing were skipped at the owner's direction; the long playback soak was deferred. The brief audio checks do not establish performance under all workloads.

Component notices are in `legal/THIRD-PARTY-NOTICES.md`.

This record does not itself publish the release or establish support for untested configurations.
