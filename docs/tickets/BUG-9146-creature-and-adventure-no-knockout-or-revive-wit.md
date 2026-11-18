---
id: BUG-9146
title: creature and adventure: no knockout or revive with three or four players
status: fixed
kind: bug
stage: Creature Adventure
reported: 2026-11-12
source: found in QA 2026-11-12 (code reading while adding the revive ring)
role: all
versions: every version with creature_rescue
fixed_in: unreleased
branch: feat/lineages-47
commits: c1ec1834
tests: tools/qa_win (manual two-PC run 2026-11-12; no lab test yet)
signature:
docs:
related: BUG-9118 BUG-9089 BUG-9141
switch: creature.rescue_party
---

## Report
Found while adding the revive ring: with three or four players a lethal hit in Creature or an Adventure is a native death; no knockout, no revive. Live before the fix: `coop.giveup down` on the MSI with four players: "down=0".

## Root cause
coop.cpp ticked creature_rescue only for a duo (players == 2) and gave it one partner state; the host could only check that one partner (and only the host could revive a guest).

## Fix
creature_rescue takes every partner's state; any standing player (not down) within 6 m for 1.5 s revives: the host or another partner for a guest, any partner for the host; an adventure captain gets back up beside any standing partner. `coop.fix creature.rescue_party off`: a duo only.

## Verification
no-fail-run: two-PC manual run, no lab test yet.
QA 2026-11-12, Creature, four players: before (build without the fix) the MSI's `coop.giveup down` gave "down=0" (a native death); after: "down=1", "MSI is down! Stand next to them within 30 s." on every game, the ring, and after 30 s "MSI fell. Progress debt: 20".

## History
- 2026-11-12: opened.
