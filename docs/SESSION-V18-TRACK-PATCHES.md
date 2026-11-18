# Track patches and session format 18

PATCH 1 and PATCH 2 are independent for each synth track and the drum kit.
They edit the final two entries of that track's modulation configuration;
older hidden track routes are retained. The GLOBAL LFO remains the control for
master-effect destinations, and its signal can also be selected as a source
for a track patch.

Session version 18 keeps the 3,584-byte record size. The low bit of each of
the eight track route-enable bytes retains its original enable state. Their
upper seven bits carry the two panel patches for that track, each encoded in
19 bits: five source bits, five target bits, eight signed amount bits and one
enable bit. Remaining upper bits are zero. The reserved master patch fields
are retained in the wire record for migration only and are not used by the
new audio engine.

On loading a v17 session, the two former shared patches are copied to each
track. Existing eight track routes are preserved, including when all are in
use. Saving or exporting after loading writes v18. Older firmware that only
accepts v17 rejects a v18 session; keep a separate original project backup if
older firmware might be used. Session version 16 and earlier remain rejected.
This is an upgrade boundary, not a guarantee that external tools already
understand v18.
