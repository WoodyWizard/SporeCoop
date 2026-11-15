---
id: BUG-1138
title: B1: fix-switch retirement, the rule, the tool and the first batch (0.7.9 and older)
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 0.7.14.1
fixed_in: unreleased
branch: feat/w51-retire
commits: a8f38fb1, 6356bbb5, 881975d3, 82c0cce9, 51e819e8, e2613445, 7fded022, eb555bc4, eef77022, fd80e750, f80e31b6, bd2f8118
tests: refactor_dump, leave_cleanup, make test (tools/fix_retire.py check --self-test)
signature:
docs: docs/10-dev-pipeline.md#pipeline-fix-retire
related: BUG-0033, BUG-0214, BUG-1128
---

## Report
Plan item B1 of docs/plan-0.7.15.md (owner decision 4; docs/audit-0.7.15/core.md CORE-1, stages.md STG-10): 784 fix
switches, none ever retired; each keeps an old code path alive next to the fixed one, and an old switch's off side
becomes a mix of code nobody shipped.

## Root cause
No rule existed for removing a switch, and the legacy command aliases (`coop.corefix`, `coop.sessionfix`) kept a
second surface on the same bools.

## Fix
* The rule, the keep list and the procedure: docs/10-dev-pipeline.md#pipeline-fix-retire. The tool:
  `tools/fix_retire.py list [--upto X] [--all] [--json]` (= `lab.py fixcheck --retire`) prints the candidates with
  their evidence (ticket run ids, recorded lab FAIL runs with the switch off), `fix_retire.py check` (make test, with
  `--self-test`) fails on a retired name registered again, named by a test or a lab tool, switched off in a current
  doc, or on a removed command. `tools/fix_retired.txt` lists every retired switch with its FAIL run.
* First batch (fixes from 0.7.9 and older): 263 matched the age and the evidence before the keep list; 221 retired
  (their `COOP_FIX`, bool and old branch deleted, the kept path as it ran with the switch on), the rest kept: unit
  seams (portable net/ / updater/ switches, header functions the unit tests call with the switch off), Space files
  and shared.cpp (the Space package), test seams (coop_monitor_plant, coop_old_species, coop_tribe_guest_tribes, suite
  guest_spot_swap, ...), feature-wide relays (creature.effects_relay, mirror.fx, nest.eggs, parts.shared_cell),
  switches other commands write (npc.play_death, npc.partners), and three whose recorded runs with the switch off all
  passed (diag.stage_end, world.leave_avatar, tribe.grab_spot: contradicted, kept).
* session.end (BUG-0033) is retired with it: the two "before BUG-0033" module lists of end_session and the Restart
  exception go; its FAIL run was made first on rel/0.7.15 (lane 3, 20261115-030257, leave_cleanup with
  SPORE_FIX_OFF=session.end: the host's ship stayed in the guest's game, the new session inherited the old remote).
* `coop.corefix`, `coop.sessionfix`, `coop.social textfix`, `coop.npc deferpuppet` and the retired entries of
  `coop.pool fix`, `coop.civ fix`, `coop.worldfix` are gone; tests use `coop.fix <name>`; the test seam settings_in
  has its own command `coop.settingsin` (a lane knob).
* Tests that used a retired switch for their FAIL twin run PASS-only; their claims say "<switch> off failed (retired
  in 0.7.15, BUG-1138: FAIL run <run>)"; each retired switch's ticket got a History line.

## Verification
* FAIL: `tools/fix_retire.py check --self-test` sees every planted case (a registered name, a test's SPORE_FIX_OFF, a
  doc's `coop.fix`, a removed command); PASS: `fix_retire.py check` on the tree, 221 retired, 0 problems.
* The session.end twin: FAIL lab 20261115-030257 (rel/0.7.15 code, SPORE_FIX_OFF=session.end, leave_cleanup).
* Refactor dumps (docs/10-dev-pipeline.md#pipeline-refactor-proof), lane 2: base 20261115-030135 (rel/0.7.15
  d0cdc8ea), after 20261115-033850: lifecycle, players, guards, hooks (212 / 210) and the message table identical;
  `fix` 784 -> 563 (the 221 retired names, plus net.async_resolve's text without its corefix note); `commands`:
  coop.corefix and coop.sessionfix gone, coop.settingsin added; `callbacks`: the custom-message lambda's hash
  (its DNA-spend branch lost the switch).
* `make -j8`, `make test` pass; object_alive ratchet 149 -> 148.

## History
- 2026-11-15: opened.
- 2026-11-15: rule, tool and first batch on feat/w51-retire (five file groups in parallel worktrees, merged).
