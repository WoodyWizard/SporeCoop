---
id: BUG-9277
title: Galaxy planet: each party has its own evolution bar (a stranger's gains no longer fill the host's bar)
status: fixed
kind: feature
stage: creature
reported: 2026-11-19
source: owner 2026-11-19 (step 4)
role: host
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 6a73ae46
tests: tools/qa_win (live TITAN QA 8+9 2026-11-19, coop.dna gain on the guest)
signature:
docs: docs/12-server-and-players.md#server-eras-built
related: BUG-9162, BUG-9266, BUG-9270, BUG-9276
---

## Report
Step 4 part 2 (owner 2026-11-18, 6N A, and 2026-11-19): each party has its own progression. On a galaxy planet the
evolution bar (cPlayer +0x10F0, the earned progress that raises the brain level and ends the stage) was one per
planet: a stranger's kills and friends filled the host's bar and every guest showed it.

## Root cause
BUG-9162 made the spendable DNA each player's own but kept the bar the party's, and on a galaxy planet "the party"
was everybody in the session: a guest's gain (MSG_DNA_ADD, a social win) went to the host's bar and MSG_DNA sent that
bar to every guest.

## Fix
party_bar.cpp (switch `creature.party_bar`): the host keeps every other party's bar (parties::table_key, the same
keys as the relations); a guest's gain and a guest's social win go to its party's bar; each guest's MSG_DNA carries
its party's bar and the guest sets it with game::set_bar (AddEvolutionPoints moves the bar both ways, the DNA is set
back). Saved next to the planet (<save>.spo.duo-progress, "DBAR" v1: the identities whose bar is the game's own, then
identity and bar); a host of another party plays by its party's saved bar. A party new to the planet starts at 0.
Test command: `coop.dna bar` (with the party bars), `coop.dna setbar <n>`.

## Verification
Live, TITAN 2026-11-19, QA 8 host and QA 9 guest of another party on Samuth (Creature):
* fix on: the guest's bar went from the host's 1078 to its party's 0; `coop.dna gain 60` on the guest: guest bar 60,
  host bar still 1078 (PASS); the host's own gain of 25: host 1103, guest 60.
* fix off: the guest's gain of 60 moved the host's bar 1103 -> 1163 and the guest showed 1163 (FAIL, the old way).
* the host's save wrote samuth.spo.duo-progress (44 bytes: one owner, two identities).
Not run yet: a reload of the saved bars, a host of another party loading the planet (the swap), a guest reaching the
brain levels by its own bar.

## History
- 2026-11-19: opened and built on feat/lineages-alpha.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
