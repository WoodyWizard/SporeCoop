---
id: BUG-0950
title: Civ: the players' city owners are not kept across a host's load: every city is dealt out anew
status: fixed
kind: bug
stage: Civ
reported: 2026-11-12
source: fix/w48-captures 2026-11-12 (code reading)
role: host
versions: 
fixed_in: unreleased
branch: fix/w48-new
commits: bb98d142
tests: civ_owners_reload
signature:
docs: docs/re/civ.md#civ-capture-keeps, docs/re/civ.md#civ-owners-kept
related: BUG-0875, BUG-0775
---

## Report
Found while fixing BUG-0875: the mod's owner table of the players' cities (g_city_owner, g_city_gifted,
g_city_captured) lives only in the host's memory. A host that loads its Civ world again (the same save after a menu,
a crash rejoin, the next session) deals every city out anew in turns: a city a guest captured, bought or was given
may become another player's.

## Root cause
Inferred from the code (confidence high): civ.cpp on_world_change and on_session_end clear g_city_owner and the
co-op tables; host_assign_cities then gives the capital to the host (or shares it) and the others in turns. Nothing
is saved. Not run.
Observed (fix/w48-new, lane 5, civ_owners_reload with civ.owners_kept off, lab 20261112-072252): the host bought the NPC
city nearest its capital (owner 0 on both games), the guest the next one (owner 1); after the host's `coop.menu` and the
load of the same slot both cities were the guest's on both games (the host's purchase dealt to the guest).

## Fix
`coop.fix civ.owners_kept` (civ.cpp): the host writes the owners of the player civilization's cities every 2 s per
world (world::current_game_key) by position and owner name, with the gift / capture marks, and gives them back at the
first split after a world change (a city within 5 m, an owner in the session). When every city has its owner back,
nothing is dealt anew. In memory only: a restart of the host's game or a new session deals the cities anew
(not covered, as before).

## Verification
* civ_owners_reload: FAIL lab 20261112-072252 (lane 5, SPORE_FIX_OFF=civ.owners_kept: "the city at (-295, 406, -69) is
  still player 0's on both games (host 1, guest 1)"), PASS lab 20261112-070917 and 20261112-074718 (lane 5).

## History
- 2026-11-12: opened.
- 2026-11-12: reproduced and fixed on fix/w48-new (civ.owners_kept).
