---
id: BUG-0173
title: Tribe: knocked-out members stand on the guest
status: fixed
kind: desync
stage: Tribe
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-civ
commits: e2a59c3
tests: tools/tests/coop_tribe_ko.py (suite tribe_ko)
signature:
docs: docs/re/tribe.md#tribe-knocked-out-members
related:
switch: tribe.ko
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The game's chieftain (and members of NPC tribes) lie knocked out with 0 health until they
  respawn; the host sends health 0, but `tribe.cpp guest_tick` applies only positive health to
  mirrors (a 0 might start the local death of a mirror), so on the guest they keep their last
  health (e.g. 0.4) and stand. Seen in play_tribe.py (enemy chief `#4 hp=0.0` on the host, `0.4`
  on the guest). The co-op chiefs use the host's health on every machine since 2026-10-23.

## Root cause
Observed (lane 7, 2026-10-28, `coop.tribe` lists `dead=` and the behaviour node now): an NPC tribe member hit to 0 on the host has the game's dead flag (+0xB5E = 1), behaviour node 0x05CA7FA7, and stays in the tribe (it was even moved, 24 s later still there). On the guest the mirror kept `hp=159.0 dead=0`: guest_tick applies only positive health, and the mirror's behaviour tree is skipped on guests (h_BTTick), so nothing plays the knock-out there.

## Fix
Fixed 2026-10-28 (fix/w2-civ, mods/coop/tribe.cpp guest_tick): a mirror whose host health is 0 gets health 0 and `game::play_death` (Die vt 0x8C + the death animation), once. When the host's member has health again, the dead mirror is removed and the next snapshot binds or spawns a fresh one. Counters `knocked_out`, `ko_replaced` in the `tribe lifetime` line; switch `coop.tribe fix ko 0`.

## Verification
Two-game test `tools/tests/coop_tribe_ko.py` (tribe fixture): the host hits an NPC tribe member with 1000 (`coop.tribe pincer`); after 3 s the guest's mirror must have the game's dead flag and no health.
- FAIL with `--nofix` (lane 7, out/lab/w2civ-manual/ko_nofix.log: guest `(hp 159.0, dead False)`).
- PASS with the fix (out/lab/w2civ-manual/ko_fix1.log, lab run out/lab/20261027-233457).
Not verified: the respawn path (the member did not get up within 60 s in any run, so the replacement of a dead mirror never ran), the look on screen (no screenshot; the dead flag and the death animation call were checked), and the player tribe's chieftain (only an NPC member was hit).

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fixed on fix/w2-civ (the guest plays the knock-out; coop_tribe_ko FAIL without, PASS with; respawn not exercised).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: tribe.ko (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
