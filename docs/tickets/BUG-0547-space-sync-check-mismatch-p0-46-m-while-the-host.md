---
id: BUG-0547
title: Space: sync-check MISMATCH p0 46 m while the host flies a rescue on the home planet (space_rescue gate)
status: fixed
kind: test
stage: Space
reported: 2026-11-04
source: lab 20261104-114611 space_rescue#3 (fix/w42-space-play, lane 2, GOG 2024)
role: host
versions: 0.7.7, 0.7.10
fixed_in: 0.7.11
branch: fix/w46-v2-civ-space
commits: 977d6ee5, fcd9ff4e
tests: space_fast_sync
signature: sync-check: MISMATCH ... (Space 40460035 ...): p: 1 apart (p0 46.2)
docs: docs/re/space.md#space-copy-on-path, docs/06-testing.md#testing-space-fast-sync
related: BUG-0378, BUG-0193
---

## Report
coop_space_rescue.py printed RESULT: OK, the lab failed it on the sync-check gate: host and guest both logged
`sync-check: MISMATCH ... p: 1 apart (p0 46.2)` at s 78.1, 4.5 s after the guest was disabled, while the host flew
the ~380 m to it with the keys (the test's `Player.rescue`, pulses of W/A/S/D) on the home planet 40460035. The
round had started with the ships 319 m apart (other runs: 24-180 m). 2 of 3 runs of the same build passed (lab
20261104-114611 #1, #2). Inferred: the speed slack (`sync.speed_slack`, BUG-0378) uses the speed at the read, and
a ship that accelerates or stops between pulses is further behind than that speed explains; or the drawn host ship
really lags on a planet at rescue speed. Not analysed (not this branch's change: the rescue flight is unchanged).

## Root cause
Observed (lab lane 9, the host's new log line under each p0 difference: the copy's distance from the ship and from the
ship's path of the last second): at a steady speed on a local lane the copy is where BUG-0378's slack (speed at the read
x (0.2 s + rtt)) puts it: no difference in ~600 checks at 1 s with W pulses, braking, zooms and a disabled guest (lab
20261107-205734, -210209, -211254, -212524). The copy was off that place only when the ship's speed changed within the
moment or a state came late: in play_space's rescue flight (lab 20261107-211833: 35.5 m from the ship, 0.0 m from its
path 0.29 s back, slack 15.5 m) and with latency spikes (lab 20261107-213732, -214244: 49-270 m from the ship, 0.0-9.9 m
from its path 0.41-0.81 s back). Each time the copy lay on the ship's path: the guest saw the host's flight a moment
late, the check compared it with where the speed of one moment says. Both sightings (space_rescue#3 p0 46.2, the 0.7.10
final check's play_space p0 50.0) and the older play_space ones (p0 57.3, 58.1, 68.9) came right after "Bob's ship is
disabled: fly close to repair it": the host's rescue flight (inferred for the old ones from the timing).

## Fix
diag_sync.cpp: the host keeps its own ship's path (every frame, the last 2.5 s); a p0 difference whose guest copy lies
within the player limit (10 m) of the path's last 1 s plus the guest's round trip is taken back (`own_path_saved` in
`coop.diag sync`). The host logs every such moment ("p0 X m from the host's ship, Y m from its path ..."). A real offset
is still caught (the path is moved to the digest's own position: `coop.diag sync force shift` gives a MISMATCH).
Switch: `coop.fix sync.own_path off` (the speed slack only).

## Verification
space_fast_sync (`tools/tests/coop_space_fast_sync.py`, a player's network with latency spikes, full-thrust pulses, the
rescue flight to a disabled guest): FAIL with SPORE_FIX_OFF=sync.own_path: out/lab/20261107-215916 (8 of 8 copies on the path taken for a difference, and the
original symptom: `MISMATCH ... p: 1 apart (p0 92.1)` on both games; earlier versions of the test: 20261107-214753); PASS: out/lab/20261107-220353 (11 copies on the path, 0 taken, no MISMATCH) and out/lab/20261107-215251 #2 (the
stricter first version of the check); play_space in 20261107-220353 passed without a MISMATCH.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-tests): G1: from the host's log the host flew ~330 m in ~8 s with key pulses; the slack uses the speed at the read (drive::speed of the newest state), so a ship between two pulses may be read slower than the path the drawn copy trails. Not reproduced, no gate change (a change needs a run where the gate is wrong and one where it is right).
- 2026-11-07 (fix/w46-v2-civ-space, 0.7.11 V2): reproduced (the copy on the ship's path, further back than its speed at the read allows), fixed (sync.own_path).
- 2026-11-08: released in 0.7.11.
