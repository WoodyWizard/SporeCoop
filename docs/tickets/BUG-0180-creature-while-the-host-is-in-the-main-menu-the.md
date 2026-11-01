---
id: BUG-0180
title: Creature: while the host is in the main menu the guest's planet fills up slowly
status: fixed
kind: bug
stage: Creature
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.2
branch: fix/w13-creature
commits: 2d444ef
tests: tools/tests/coop_away_refill.py (suite away_refill)
signature:
docs: docs/re/creature.md#creature-proxy-guest-side
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The host leaves to the main menu: the guests are told and play on alone (their own game runs
  their NPCs again, `coop.npc` host_away=1), but the game respawns the removed NPCs only a few at a
  time (`local_alive` 0-1 after 30 s in `coop_exit_menu.py`). Making the mirrors local NPCs
  instead failed (a mirror with AI leaves its herd and is destroyed, docs/re/creature.md 8.3).

## Root cause
Observed (lane 4, 2026-10-30, `coop_away_refill.py`): the host's "Exit to Main Menu" changes its
world, and its world change despawns every NPC at the guests a few seconds before the guest learns
`host_away` (so `away_proxies` found nothing to keep). The guest's update filter had removed its own
game's NPCs all the time, and the nests then brought their members back one at a time: 0 own NPCs
within 70 m of the guest for 25 s, 1 after that (the guest had shown 11 of the host's there).

## Fix
`npc.away_locals` (npc.cpp): while the host plays, the guest keeps the last non-empty set of proxies
it showed (species, place, orientation; only proxies in a nest herd of its game), every 0.5 s. When
the host is away (within 15 s of that set) it creates a creature of its own game for each with
`cCreatureAnimal::Create` in the nest herd of that species, as the nest does, at that place. Its own
game runs them (no puppets, no mod pointers kept). When the host is back the update filter removes
them as before. `coop.fix npc.away_locals off`: the proxies just go (0.7.3.1).

## Verification
`coop_away_refill.py` (suite away_refill): the guest's own NPCs (census `local`, AI on) within
70 m 10 s after the host left, at least 60 % of the host's NPCs it showed there before.
* FAIL with `SPORE_FIX_OFF=npc.away_locals`: out/lab/20261030-140802 (0 of 11 for 25 s).
* PASS: out/lab/20261030-140958 (2 runs, `--repeat 2`; 11 of 11 at once, "20 of its NPCs shown here
  go on as NPCs of this game").

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-creature (wave 3): not worked on beyond reading; still open. What is left is how the game's own population refills a planet (its respawn rate of removed herds), which needs reverse engineering of the creature spawner; the known fallback (mirrors made local NPCs) failed before (docs/re/creature.md 8.3). Not a quick fix.
- 2026-10-30: fix/w13-creature: reproduced (0 own NPCs near the guest for 25 s) and fixed by npc.away_locals.
- 2026-11-01: released in 0.7.3.2.
