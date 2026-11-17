---
id: BUG-9247
title: Creature: a partner's pack keeps a recycled creature (another species) and teleports it to the partner
status: fixed
kind: bug
stage: Creature
reported: 2026-11-16
source: QA TITAN 2026-11-16
role: host
versions: 0.7.14.1-lineages63-dev (unreleased first BUG-9245 fix)
fixed_in: unreleased
branch: feat/lineages-47
commits: ae5c0ee5
tests: manual Windows QA with tools/qa_win/qa.py (TITAN, Samuth.spo, host 9 + guest 8)
signature:
docs: docs/re/creature.md#creature-summon-pool
related: BUG-9245, BUG-9129, BUG-9233, BUG-0044, BUG-1100, BUG-9248
---

## Report
TITAN QA 2026-11-16 (owner-requested pack test, Samuth.spo): the partner's two summoned flock members (10 hp, species 067DA...) turned into two 0654E5F0 creatures (62.5 hp) still listed in the partner's pack; the pack teleported them beside the partner (teleports 3 -> 8) and they took the partner's puppet as their target (npc.pack_spares_partners cleared it 100 times in 16 s).

## Root cause
Observed (runtime):
* The summon event's end (vt+0x34) returns every creature still in its herd's list to the game's pool: right after the end the flock was at (0 0 0) with herd 0 (`coop.gpack`: dist 505.6, herd 00000000), with the first BUG-9245 fix and with the game's own end alike.
* The pool hands such an object out again as another creature (a nest's spawn, BUG-0044 / BUG-1100) with the same serial (+0x24): guest_pack's alive check (serial, alive, not dead, health) kept it.
* guest_pack pulled the pooled members back beside the partner (lost: over 60 m), so the game later gave those objects to a hostile herd's spawn while the pack still held them.
* A summoned creature's species key (species_instance) reads 0 a few seconds after the summon while its species profile stays: the key cannot tell a reused object.

## Fix
- BUG-9245's list tidy: the summon's end no longer finds the partner's flock in its list, so it is not pooled (drama.cpp tidy_summon_list).
- creature.pack_member_reuse (guest_pack.cpp alive): a member whose species profile changed since it joined, or that sits in the pool (no herd, at the planet's centre), leaves the pack (`reused=` / `pooled=` in `coop.gpack`). `coop.fix creature.pack_member_reuse off`: 63-dev.

## Verification
FAIL (the first BUG-9245 fix, TITAN Samuth.spo 21:06-21:09): the members 245C4848 / 25100BD0 became species 0654E5F0 at 62.5 hp in another herd, still pack members, teleported to the partner, aimed at it.
PASS (21:22-21:45): the pool check caught 4 members at (0 0 0) 0.1 s after the old end (before the tidy); with the tidy, 5 and then 3 summoned members stayed in the pack and in the game's update list, reused=0 pooled=0, until they died fighting (dropped as dead).
no-fail-run: Windows QA by hand (no lab on this PC); a first species check by key dropped all 4 members after 7 s (the key reads 0): changed to the profile, not seen since.

## History
- 2026-11-16: opened and fixed in the same QA session (feat/lineages-47, unreleased).
