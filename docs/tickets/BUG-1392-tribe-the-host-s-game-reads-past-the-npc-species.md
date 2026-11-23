---
id: BUG-1392
title: Tribe: the host's game reads past the NPC species list once when its update calls FormTribe less than 1 s after a village formed (tribe.form_species decided with the tribes before) (dc04aee5:Resource::StandardFileFilter::IsValid+0x1FEF)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-21
source: lab
role: host
versions: 0.7.16 dev (coop 0.7.15.5 build of rel/0.7.16 21025f40)
fixed_in: 0.7.16
branch: fix/w62-core
commits: c1c0a136
tests: tools/tests/coop_tribe_form_species.py (tribe_form_species, twin tribe_form_species_fresh_off)
signature: dc04aee5:Resource::StandardFileFilter::IsValid+0x1FEF
docs: docs/re/tribe.md#tribe-form-species-fresh, docs/06-testing.md#testing-tribe-form-fresh
related: BUG-1233, BUG-1238
---

## Report
Third final check of 0.7.16 (lab 20261121-003202, nightly, lane 2): tribe_form_species FAIL "no fault in the host's
game while its update calls FormTribe (1 new)", "formed the villages its property list allows: 6 tribes ([4])", "no
fault after coop.tribe form". It had passed 9 of 9 runs before (since 2026-11-18). Host fault: 0xC0000005 at 0x00406EFF
(data 0x50B, ebx = the controller 0x1E440EE0), chain FormTribe+0x119 (0xCC6559); crash_triage: the BUG-1233
signature. The guest had no fault.

## Root cause
Mod bug, old (BUG-1233, 0.7.14), not a w61 regression (the only w61 tribe.cpp commit is 34cb98e5, BUG-1375, the
guest's village copy). `tribe.form_species` decides "FormTribe would read past the species list" once a second
(species_holds caches the answer for 1.0 s of `H->time_seconds`). The game's update 0xCC6A90 calls FormTribe when the
timer 0x157E940 (set to 1.0 by FormTribe, counted down by the frames' game dt) runs out: one second of game time.
Host log: `coop.tribe form` at s 20.236 decided with 2 tribes ("in the list") and formed the third; the update's next
call came at s 21.234, 0.998 s later on our clock, took the kept answer and FormTribe read list[min(2, 3 - 1)] = the
planted 0xFFFFFFFF: one fault (caught by the game's handler; the relationship loop of that second skipped). The next
calls decided again and formed villages 4 and 5 (tribe.form_last_species), so the test saw a fault and 5 of 6 tribes.
In a real game the same happens when a village forms while its species is the list's last: the next second's call can
read past the list (a fault, or a crash with a worse value there, as BUG-1233).

## Fix
`tribe.form_species_fresh` (tribe.cpp, h_Form / form_last_species): a call that reached the game's FormTribe drops
the kept answer (form_species_recheck), so the next call decides with the tribes it has. Held calls (the update's
every-frame calls while held) still use the second's answer. `coop.tribe form_species` prints `fresh_fix=` and
`fresh=` (answers dropped). Test: right after its `coop.tribe form` the test writes the update's timer 0x157E940 to 0,
so the game's next FormTribe comes in the next frame, inside the kept second: the race on every run.

## Verification
Was waiting for the lab run (no lab runs in this task):
- PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_form_species --lane N --retry 0 --no-build`
- FAIL expected (twin): `... run tribe_form_species_fresh_off ...` ("no fault in the host's game while its update
  calls FormTribe" FAIL, a fault at 0x406EFF).
- The FAIL run of the bug itself, before the fix: 20261121-003202 tribe_form_species.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_form_species PASS in 20261122-194210 and 20261123-023643; the twin tribe_form_species_fresh_off FAIL in 20261122-191140 (a host fault while its update calls FormTribe). Closed.

## History
- 2026-11-21: opened.
- 2026-11-21: fix/w62-core: cause from the final check's logs, fix tribe.form_species_fresh, test forces the race.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
