# MIDI clock and note-latency design review

Source review only, 16 November 2026. No MIDI timing experiment or device test was run for this review. The following is a design analysis, not a measured latency or a proposed untested release change.

## Current path

USB and TRS MIDI messages are timestamped at receipt, queued and drained by the foreground; played notes are then ordered in the audio event ring. `BR_NOTE_DELAY_MS` is 6 ms, and notes are admitted at the first audio block boundary at or after that timestamp plus the delay. The configured 3029 audio half is 256 frames at 44.1 kHz, about 5.80 ms. This is an intentional scheduling delay and block quantization, not a measured end-to-end latency; foreground backlog, rendering, output buffering and converters also matter. Ordered events can wait behind an earlier not-yet-due event. Queue-pressure handling deliberately accelerates waiting events and prioritizes note-off safety.

External clock is a separate timestamped 24-PPQN stream. The follower estimates tempo over a beat and slews phase corrections; a scheduler projects future boundaries from that phase with a bounded correction. START/CONTINUE wait for the next clock, and clock loss causes a stop. Timestamp-to-audio mapping uses the DMA anchor. This design filters jitter, but any phase estimate must trade tracking against noise, and ambiguous missed pulses can produce phase error. It cannot make an event audible before the event arrives or guarantee exact alignment to a jittery, delayed source.

## Recommended work, in order

1. **Separate note responsiveness from clock filtering.** Map note receipt timestamps into the physical audio frame timeline and admit them at the first safe renderable sample, rather than adding an unconditional 6 ms. Retain ordered note-offs, voice stealing, panic-on-overflow and recording timestamps. Reducing the delay without changing block scheduling can increase latency variability and expose late foreground events.
2. **Reduce boundary quantization.** Investigate splitting the render block at timestamped note and sequencer events, or a smaller scheduling quantum that preserves the existing 256-frame DMA transfer. This may add CPU and memory cost; it cannot change samples already rendered into the output buffer. Do not claim a latency reduction until measured at audio output.
3. **Keep phase anchored to incoming clock.** Preserve arrival timestamps and the 24-pulse count, map to the output timeline as close to receipt as practical, and correct future step boundaries from phase error with bounded slew. Tune a fast, low-noise phase tracker for small tempo changes; avoid blindly resetting phase or smoothing individual pulse periods in a way that introduces bias. Define what happens on ambiguous missed pulses, clock loss, restart and source changes.
4. **Address queue contention.** Review the shared event ring and foreground work for head-of-line waits; give real-time clock and safety events a bounded path without reordering simultaneous note-ons, note-offs or transport. Bound backlog and preserve fail-safe overflow behavior.
5. **Establish a measurement plan before implementation.** Measure input timestamp to analog/USB audio onset, clock phase error and jitter distributions under light and worst-case loads, at several tempos, for USB and TRS independently. Include START, CONTINUE, lost clocks and recovery; use a common external reference and report median, tails and worst cases. This is a future validation plan, not testing performed under the present request.

The 3029 implementation and its existing host tests remain unchanged. Hardware clock tests were explicitly deferred for this review.
