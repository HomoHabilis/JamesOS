# MODEL modulation experiment

This worktree is an exploration. MODEL is connected to playback only when
`BR_MODEL_MOD_EXPERIMENT=1`; the ordinary build leaves it disabled. It starts
with the integration worktree's uncommitted state. No hardware was changed.

The Plaits adapter reconstructs its active engine when its model changes. The
normal mono arena fits every model, but a polyphonic voice is usually allocated
an 8 KiB arena; models 15, 18 and 19 need 16 KiB. Asking a small voice to render
a large model deliberately produces silence. Engine reconstruction also occurs
inside rendering, where six simultaneous changes or multiple tracks changing
at once could exceed an audio deadline. The sample-rate conversion FIFO can
delay when a requested model actually takes effect.

## Prototypes in this checkout

* `br_poly_source_init_capacity` can reserve full-capacity voices while starting
  them on any valid model. The normal build keeps its compact allocation; the
  experimental poly scheduler uses full capacity and the existing delay loan.
* `instrument_model_sweep.h` is a small latest-request-wins planner that selects
  at most one pending voice per numbered audio interval. It records an older
  prepared model accurately if a newer request arrived in flight. It neither
  owns nor fences voices; the experimental audio scheduler chooses one voice
  before dispatch, prepares it inside that voice's worker job, and commits the
  result after the jobs have joined. Mono model changes are limited to one track
  per source frame, rotating priority across tracks.
* MODEL is a stepped, bipolar destination around the saved model. Full depth
  spans the 24 model indices; values clamp at the first and last model. Only the
  effective model changes: the saved sound and other parameters are untouched.
* The host tests cover all 24×24 model transitions with a held gate, real active
  engine checks and arena canaries, plus six full-capacity voices. The planner
  tests cover request replacement and failed/uncommitted preparation.

For the FM-1 host configuration, a full voice is 23,576 bytes. Four fit in the
existing DSP storage; the remaining two require a 47,152-byte delay-memory loan.
That loan is already used for the large Plaits models and has the existing
delay/punch tradeoffs. These are host-build layout measurements; the target
build and actual worker configuration must still be checked.

Run `python3 tests/run_poly_units.py` and `python3 tests/run_model_modulation.py
--reuse-dsp` for the voice and modulation checks. Run `python3
tests/run_poly_two_cores.py` before `python3 tests/run_model_poly_scheduler.py`;
the scheduler test reuses its per-core DSP objects to compare actual serial and
parallel audio. `python3 tests/run_model_transition_stress.py` builds its own
isolated non-poly DSP objects for the full-voice transition stress and timing
report.

## Remaining integration

The path is not qualified for release. Staggering limits the number of cold starts per interval but
does not prove that one cold start meets the deadline or that several tracks
cannot collide with other expensive work. It also spreads the model change across
voices in time, and the source FIFO can carry old-model samples after a switch.
The host stress test observed large sample discontinuities at some transitions;
their audible effect has not been assessed on the instrument. In the experimental
build, live polyphony reserves full-capacity voices even when MODEL is not being
modulated, so the delay-memory loan and its PUNCH echo limitation also apply.

Before enabling the feature, measure worst-case switches on the FM-1 under
simultaneous modulation, six held voices, other active tracks and effects.
Exercise every destination, especially 2–4 and 15/18/19, with the diagnostic
worst-half and overrun counters described in `DIAGNOSTICS.md`. Compare against
an unchanged baseline, and test note-off, voice stealing, transport, denied
loans, worker faults, rapid retargeting, and project save/load. Host timings and
successful builds are not proof of target real-time safety.
