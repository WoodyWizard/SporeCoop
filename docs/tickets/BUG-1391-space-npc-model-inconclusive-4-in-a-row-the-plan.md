---
id: BUG-1391
title: space_npc_model INCONCLUSIVE 4 in a row: the planted visit leaves the star before a ship asks for its model
status: fixed
kind: test
stage: Space
reported: 2026-11-21
source: final check 20261121-003202
role: 
versions: unreleased
fixed_in: unreleased
branch: fix/w62-flake-tcs
commits: 50052a59, fa98a003
tests: space_npc_model, space_npc_model_coop
signature:
docs: docs/06-testing.md#testing-fixcheck-nan-claims
related: BUG-1363, BUG-1213, BUG-0546
---

## Report
0.7.16 third final check (nightly 20261121-003202, rel/0.7.16 21025f40): space_npc_model (lane 3, `--solo`) FAIL as
the 4th INCONCLUSIVE in a row (A15 streak): "visit 3: planted: the next 4 ship model(s) get the planted unknown key
0BAD0546/..." then "visit 3: ship models defaulted 0", RESULT: SKIP. The BUG-1363 seam (w61, c67ac9af) was in the
build; space_npc_model_coop of the same run (lane 6) PASSED with "visit 3: ship models defaulted 4 (planted)".

## Root cause
Test, not mod; not a regression of a w61 fix (BUG-1363 only made the third arrival planted; it did not change when
the game asks for a model). Observed:
- solo host log: third arrival `Space 80460035 -> Space 804A0014` at 07:56:21.146, the log ends 07:56:56 (the test
  left after fly()'s fixed wait, ~35 s): no "replaced by the planted unknown" line at all, so no ship asked for a
  model while the plant was armed.
- coop host log, same run: third arrival at s 255.4, the planted models asked for at s 276.2 (2 ships, +21 s) and
  s 311.3 (2 ships, +56 s): ships that came to the star later.
- every natural case in the history came at the FIRST arrival, ~2 s after it (fix/w42-infra 20261104-130939,
  fix/w46-advance-reg 20261107-193455: `29133E22 ... is not in this game` 1.8 s after the stage change), never on a
  revisit: over all lab logs "visit 1: defaulted 2" twice, visits 2-4 defaulted 0 in 36 of 36.
Inferred: the empire's ships at its star get their gfx/model at the first arrival; a revisit asks SetModel only for
ships that come later (tens of seconds), so a plant armed before a revisit is consumed only if such a ship comes
within the fixed wait. The coop run stayed longer (detector samples, guest checks) and caught it; the solo run did not.

## Fix
tools/tests/coop_space_npc_model.py: the second arrival is planted (a second natural arrival never brought the case),
and after it the test waits for the state: `ship model keys planted N` in coop.status > 0, at most 180 s (PLANT_WAIT),
confirming cinematics as fly() does, then 3 s for the game's SetVisible / gfx messages on that ship. The run prints
how long it waited. No check changed; SKIP stays for a run where no ship asked for a model in 180 s. No mod change.

## Verification
no-fail-run: test-only change (the test waits for the planted model state); open, waiting for the lab run (no game
today, owner rule). Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run space_npc_model space_npc_model_coop --lane N
--retry 0 --no-build` must PASS (not SKIP), each log with "visit 2: planted model keys asked for N after S s" and
"ship models defaulted N (planted)" (or "(natural)" at visit 1). The FAIL side of the guard is BUG-1363's twin
(`SPORE_FIX_OFF=game.ufo_gfx ... creature_ufo_nomodel` must FAIL).

## History
- 2026-11-21: opened.
- 2026-11-21: test waits for a ship that asks for its model after the planted arrival (fix/w62-flake-tcs).
