---
id: BUG-0011
title: Creature: the host sees the guest as its own species / not its changes
status: not-reproduced
kind: desync
stage: Creature
reported: 2026-10-25
source: player screenshots
role: host
versions: 0.7.2.1
fixed_in: 
branch: fix/creature-desync
commits: 
tests: creature_desync
signature: 
docs: docs/known-issues.md
related:
---

## Report
"He is my species from my view", "I don't even see his changes".

## Root cause
Not reproduced on 0.7.2.2 after editors, rejoins and reloads. Two 0.7.2.1 bugs explain it and are fixed in 0.7.2.2 (BUG-0004: creations sent without records, re-apply failing on rejoin).

## Fix
None separately.

## Verification
The test checks the host shows the guest's edited creature after the editors and after a reload.

* Re-check 2026-11-08 (fix/w46-n2-desyncs, N2 of 0.7.11, rel/0.7.11 ccfb0ae4, lanes 9/10): the host shows the guest's
  own edited creature after the editors, a crash and a reload, and after rejoins: creature_desync, nest_species,
  legless_guest_cancel OK out/lab/20261108-030127; creature_desync, creations_rejoin, realign_creature_rejoin, puppet_after_events (the
  partner's creature within 2 m after a rejoin, the vote's editors, deaths) OK out/lab/20261108-032648 with SPORE_NET=players and the guest
  at 20 fps (SPORE_FPS=guest:20). Why the current code does not show it: the puppet's body comes from the guest's
  creation records (BUG-0004 sends them with the creation, 0.7.2.2) and a rejoin applies the creation again under the
  reused id (BUG-0076); both are exercised by these tests.
* no-fail-run: not reproducible since 0.7.2.2.

## History
- 2026-10-25: reported (player screenshots).
- 2026-10-25: not reproduced, see Root cause.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-30: fix/w13-creature: tried again: creature_desync (editors, a crash and a reload, the host shows the guest's edited creature) and nest_species OK in out/lab/20261030-141545 (fix/w13-creature, lane 5, GOG 2024); legless_guest_cancel OK (out/lab/20261030-142629, both screens show the right species after editors and a rejoin). Not reproduced; stays.
- 2026-11-08 (fix/w46-n2-desyncs, N2): not reproduced again on the current build, plain and over the players' network at 20 fps (7 tests, out/lab/20261108-030127, out/lab/20261108-032648); closed with that evidence.
