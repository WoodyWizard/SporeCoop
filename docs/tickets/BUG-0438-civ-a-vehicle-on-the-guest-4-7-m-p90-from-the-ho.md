---
id: BUG-0438
title: civ: a vehicle on the guest 4-7 m (p90) from the host's under loss bursts (sync_burst, sync_fps15burst)
status: not-reproduced
kind: desync
stage: Civ
reported: 2026-11-01
source: players suite, lab 20261101-160125 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in:
branch: fix/w46-k3-bodies
commits: 0f0f32f1, fbeb2cfa, 836b0b5e, 47be862f
tests: body_trail_civ, body_trail_civ_loss, sync_civ_loss
signature:
docs: docs/re/civ.md#civ-place-now, docs/06-testing.md#testing-bodyrec-diag
related: BUG-0425, BUG-0186
---

## Report
The players suite's burst variants of `sync` (coop_sync.py, every stage's objects on both games) failed Civ's
`civ/vehicles: 90% within 3.0 m` in both runs that had loss bursts (SPORE_NET=burst: rtt 70 ms, 30 % loss for
~3 s every ~20 s; lab 20261101-160125): `vehicles n=1 pos med 1.57 p90 7.32 max 8.57 m` (sync_burst, lane 9) and
`med 2.68 p90 4.26 max 4.44 m` (sync_fps15burst, the guest also at 15 fps, lane 10). The same run's
sync_mobile (3 % random loss, 120 ms) and sync_fps15 passed; the other stages' objects stayed within their
limits under the same bursts. Only one vehicle was sampled (n=1), so the evidence is thin.

How to reproduce: `SPORE_LANE=N SPORE_NET=burst python3 tools/tests/coop_sync.py` (the Civ part).

## Root cause
Not analysed. Inferred: during a burst the mirror extrapolates the vehicle (civ.extrap, civ.vel_cap) from
its last state; a driving vehicle that turns or stops meanwhile is several metres off until the next state.
Observed (2026-11-06, fix/w45-remote-bodies, coop_body_trail.py --stage civ: both games' own positions at the same
moment): the mirror is moved on with its estimated velocity at most kVehAhead 0.15 s past its last state (civ.extrap,
BUG-0186). A state's age is the send interval (0.1-0.13 s at 30 fps) plus the latency and a frame, so whenever states
come late (a loss burst, a loaded host) the mirror stops at 0.15 s and falls behind by the rest: lag p50 0.09-0.22 s,
p90 0.22-0.49 s, the vehicle p90 2.8-5.5 m behind at 15-25 m/s (labs 20261106-021152, 20261106-031151). The velocity
estimate after a turn (this ticket's inference) is a smaller part: with the longer limit the p90 is 1.8 m.

## Fix
No fix for the reported trail is needed at a normal load; one defect found on the way is fixed: `coop.fix civ.place_now` (civ.cpp guest_tick): the mirror goes on with the estimate's velocity and only the rest is glided, aimed one frame ahead; before, it was drawn a frame's glide plus a frame behind its own target (drawn vs target p50 0.98-0.99 m off, 0.25-0.46 m on, same load; docs/re/civ.md#civ-place-now).

## Verification
no-fail-run: the reported trail is not reproduced at a lab load under ~15 on 6 cores, with or without the fix. body_trail_civ_loss: on out/lab/20261107-105916 3 of 3 (p90 1.11 / 1.89 / 1.37 m), SPORE_FIX_OFF=civ.place_now at the same time out/lab/20261107-105916-2 3 of 3 (1.50 / 1.36 / 1.35 m; p50 0.69-0.84 against 0.53-0.57 m on). body_trail_civ: on out/lab/20261107-101400 3 of 3 (p90 0.94 / 1.48 / 0.82 m, p50 0.36-0.48), off out/lab/20261107-101450 3 of 3 (1.50 / 1.54 / 1.18 m, p50 0.62-0.67). At a load of 11-16 both fail alike: on out/lab/20261107-082432 0 of 3 (p90 4.31 / 4.00 / 2.97 m), off out/lab/20261107-082632 0 of 2 (4.45 / 3.34 m), also 20261107-074040 (0.7.9 code, 0 of 3): both Civ games at 16 fps, the host's states every 0.14 s (p50) to 0.39 s (p90), the target's error grows with the state's age (coop.bodyrec diag rows, docs/re/civ.md#civ-place-now). That is the host's frame rate, not the mirror's driving; the 0.7.9 final-check failures (20261107-012856, -004042, 20261106-231404) ran at such loads.

## History
- 2026-11-01: opened.
- 2026-11-01 (feat/w33-tests-session): again in a rerun of sync_burst (lab 20261101-173700): `vehicles n=1 pos med 0.13 p90 3.53 max 4.20 m`, raw p90 6.00 m.
- 2026-11-04 (fix/w41-civ-combat): reproduced with suite `sync_civ_loss` (coop_sync.py civ under SPORE_NET=burst):
  1 of 4 runs fails `civ/vehicles: 90% within 3.0 m` (p90 4.13 m, max 9.58 m; out/lab/20261104-020256, 4 runs:
  p90 1.57 / 4.13 / 2.15 / 1.56 m). The same loss all the time (SPORE_NET="delay=35,jitter=10,loss=0.3") passed
  with p90 0.91 m (out/lab/20261104-020029): the gaps of a 30 % loss alone do not explain it. Tried: a longer
  extrapolation (0.6 s instead of 0.15 s) for 5 s after a gap in the state cycle numbers (a loss, not a host
  hitch): 1 of 4 runs failed again (p90 2.66 / 3.11 / 1.06 / 1.38 m, max 10.5 m; out/lab/20261104-020316), no
  measurable gain, so it was not kept. Observed: the large samples come with large facing errors (14-45 deg):
  the test's vehicle turns round (coop_sync's stir drives it 40 m away and back) while the states are lost.
  Inferred: the velocity estimate (smoothed 0.6, from the last states) still points the old way after a turn
  inside a burst, and with n=1 and 16 samples one such moment decides the p90. Next: send the vehicle's own
  velocity (or its order target) in the state instead of estimating it; and sample more vehicles / rounds
  in coop_sync for Civ so one burst does not decide a run. Status stays open.
- 2026-11-06 (fix/w45-remote-bodies, M1): root cause observed (the 0.15 s extrapolation limit), fixed by drive.lead.
- 2026-11-07 (orchestrator, 0.7.9 final check on a quiet lab): reopened, the claim does not hold: body_trail_civ_loss 1 of 2 with the lead (p90 3.35 m in #2, out/lab/20261107-012856) and 1 of 2 with SPORE_FIX_OFF=drive.lead (p90 4.03 m, out/lab/20261107-012856-2); FAIL in the final check 20261106-231404 (5.30 m) and the rerun 20261107-004042 (2.54 m). The lead is better than or equal to 0.7.8 behaviour, so it ships as an improvement (CHANGELOG Changed), the rest goes to 0.7.10 K3.
- 2026-11-07 (fix/w46-k3-bodies, K3): decomposed with coop.bodyrec diag rows: the drawn mirror behind its own target (fixed, civ.place_now) and the target's error from the host's state ages under lab load; at a moderate load body_trail_civ_loss and body_trail_civ pass 12 of 12 with and without the fix. Closed not-reproduced with these runs.
