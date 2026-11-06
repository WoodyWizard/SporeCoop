---
id: BUG-9062
title: Late Tribal walk qualification flags large relocations also present on host
status: open
kind: bug
stage: Tribe
reported: 2026-11-05
source: Connected native Clyth five-victory campaign movement qualification
role: both
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_tribe_walk_connected.py
switch:
signature: member28 host relocates at43.12s guest follows at43.26s
docs: docs/re/tribe.md
related: BUG-0386 BUG-0410
---

The unchanged upstream no-jump criterion fails two30-FPS connected walks.
First raw evidence: tribal-validation-walk-guest30.json/.log. Host member28
changes position by about174m at43.1245s while native speed is zero; the guest
position follows at43.2596s, drawn position at43.3138s. The guest tracking
target/speed remain stale at the old location (rv4.72), so its walking-only
check flags a174.37m jump and1.09m follow-up. Host walking-only check excludes
the authoritative move because speed is zero. Do not call this a proven
guest-only collision/stutter reproduction of BUG-0386.

Second raw evidence: tribal-validation-walk-guest30b.json/.log. Host member28
relocates12.44m at0.94s, again with zero speed. Guest flags13.12m at1.19s,
3.65m at1.23s and7.10m at1.26s. Origin of the authoritative relocations and
post-relocation overshoot is unproven. Keep both failures; do not weaken the
movement limits or quietly exclude the chief. The fixture uses explicit
selection and normal native walk orders, not actor teleports during recording.

Outside these events, guest matched-speed windows are100%, gait98-100%,
citizen path-relative position p90 is0.074m then0.050m, and both games report
zero faults. These aggregate results do not erase the failed jump criterion.

Additional bounded coverage: tribal-validation-walk-host30.log passes the
existing jump/window/gait limits for the surviving host chief as seen by the
guest; its slow-speed sample is insufficient, explicitly NOT MEASURED.
tribal-validation-mobile-b.log passes a45-second, nine-member guest walk at
30 FPS through the existing mobile emulator (120ms one-way delay,60ms jitter,
3% loss,1% duplicates, seed61). No jumps,100% coverage, citizen path-error
p900.226m,100% matched windows and gait,1% uneven slow frames, zero faults.
The wrapper then restores the direct connection and verifies both founders.
This does not reproduce BUG-0386 or close either earlier relocation failure.
