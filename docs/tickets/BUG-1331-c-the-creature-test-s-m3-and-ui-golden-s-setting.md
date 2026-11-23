---
id: BUG-1331
title: C: the creature test's M3 and ui_golden's settings references still expect one shared DNA pool (BUG-1291 own DNA)
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: plan
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w59-tests
commits: d679a5e5 7459b4da
tests: creature, ui_golden
signature:
docs: docs/06-testing.md#testing-lab-mem-reserve
related: BUG-1291, BUG-0864, BUG-1149
---

## Report
The 0.7.16 final check (lab 20261119-214823, rel/0.7.16 f6c16890, triage package C) failed two tests that do not know
own DNA per player (BUG-1291, session setting `all.shared_dna`, default off):

* `creature` (lane 6): only the M3 step: `dna host 0.0 -> 5.0, guest 255.0`. The check asserted the guest's DNA equals
  the host's after a gain (the old shared pool).
* `ui_golden` (lane 3): `settings_menu` 4.24 %, `settings_host` 5.26 %, `settings_guest` 4.24 %, region from screen
  y 482.

## Root cause
Tests, not the mod.

* creature: the host's log shows `dna: the guests' own DNA of 1 players read from the store of game0/scalinus.spo`,
  `dna_own=1:250`, then `party +5.0 -> every own DNA` and `dna_own=1:255`: the gain went to every wallet as designed;
  the guest's wallet of 250 was left in the lane's per-player store by an earlier test (coop_two_instances.py does not
  go through twoinst.start, so it never called forget_store / forget_settings). The check `guest == host` asserted the
  shared pool.
* ui_golden: the settings page (coop_ui.cpp settings_entries: "All stages" first, rows in kDefs order) now has
  "Shared DNA" as row 5 of page 1 (Off, tag "session"); every row below moved one line down and "Respawn next to the
  partner" moved to page 2. Checked in the diff image of the run. Expected change of BUG-1291.
* Confirmed in the code: `all.shared_dna` is `Scope::Session` (settings_core.h kDefs, default 0, off_in_rivals);
  settings_core.h can_set refuses a Session setting once the session started, so the host sets it in the lobby
  before the start (the settings page of the galaxy menu, the `settings_menu` golden step); in play it shows "locked".

## Fix
* coop_two_instances.py M3 rewritten for the own-wallet model (the old check asserted the wrong thing: one pool):
  the host gains 4, the guest spends 4 of its own, the guest gains 5: each game's own DNA (`coop.dna bar`, the game's
  value) rises by 5, the host's does not follow the guest's spend, the guest keeps its difference, both evolution bars
  equal (the party's). Then the shared pool: `all.shared_dna` forced on, the guest's DNA becomes the host's, a gain of
  the guest raises both alike; the setting is put back. Key frame `own_dna`. The test now starts without the per-player
  store and settings of an earlier test (twoinst.forget_store / forget_settings).
* ui_golden: the three settings references must be made again from a lab game (`coop_ui_golden.py --update --only
  settings_menu,settings_host,settings_guest`, as BUG-0864 did) on the merged tree, looked at, committed with the
  reason. Pending the lab.

## Verification
Pending the lab ("lab ok"): creature PASS; twin `SPORE_FIX_OFF=creature.own_dna` must FAIL ("the guest's spend of 4
came off the guest's DNA only"); ui_golden PASS after the references are refreshed.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): creature PASS in 20261122-194210, 20261123-022904, 20261123-023643; the twin SPORE_FIX_OFF=creature.own_dna FAIL in 20261120-132626 ("the guest's spend of 4 came off the guest's DNA only"); ui_golden PASS with the new references in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened; M3 rewritten, golden refresh prepared (fix/w59-tests).
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
