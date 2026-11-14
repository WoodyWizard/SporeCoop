---
id: BUG-1067
title: Civ: savestrip takes the game's own empty herds out of Civ saves (they look like the herds the mod makes)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in: 0.7.14
branch: fix/w49-tests
commits: b359498b
tests: civ_save_herds, save_hygiene_leave, civ_save_strip
signature:
docs: docs/re/civ.md#civ-save-herds
related: BUG-0973, BUG-0255
---

## Report
Found by fix/w49-civ (BUG-0973) while checking that wild-creature proxies stay out of a Civ guest's save
(civ_save_strip, lane 4 + Carol on another lane, lab 20261113-043251): `coop.savestrip` after Carol's save says
`empty_herds_stripped=77` (and `ours=9` herds made for proxies). In Civ the game's own herds have no archetype (+0x88)
and no nest (docs/re/civ.md#civ-wild-sync), which is savestrip's "own kind" test (BUG-0255: an empty own-kind herd
is stripped), so empty herds of the game itself are probably left out of every Civ save, the host's too.
Reproduce: a Civ world, `coop.save`, then `coop.savestrip` (empty_herds_stripped > 0 without any partner's puppet).

## Root cause
Inferred: savestrip's own_kind_herd cannot tell a Civ game herd from ours. Not checked: what an empty game herd
left out of a save means after the load (the reloaded world ran 10 s without a fault, no null seen; a nest or
migration that refers to it?).
Observed (fix/w49-tests, lane 5, civ fixture, host + guest): the host's Civ world has 82 herds, 78 of them without an
archetype and a nest, 71 of those empty (wild creatures pooled away from the camera leave their herd empty); none of
them is the mod's (no puppet in Civ, the guest's proxies live in the guest's game). With the old rule the host's own
`coop.save` stripped 72 empty herds (savestrip.cpp creature_filter: own_kind_herd + strip_empty_herd). After the load
the game had 82 herds again, but 52 of the 72 were not back at their home (same species within 2 m): the game makes
new herds at load for the missing ones elsewhere, so the wild creatures' herd homes (where they live and come back)
were reshuffled on every Civ save and load, host and guests alike. No fault in 20 s after the load.

## Fix
game.cpp keeps every herd the mod made (create_herd, and a herd own_herd_for hands out again) by its serial
(cGameData::mID +0x24): `game::our_herd(h)`. savestrip's empty-herd rule (BUG-0255) strips an empty own-kind herd
only when it is ours; an empty game herd is kept (counted `empty_game_herds_kept` in `coop.savestrip`). The rule for a
herd that still holds our creature (puppet, proxy, pending destroy) is unchanged. Switch `savestrip.our_herds` (off:
by the look alone, 0.7.13).

## Verification
`civ_save_herds` (coop_civ_save_herds.py, civ fixture, ~2 min): every herd of the host's Civ world (by species and
home) is back after its save and a load alone, and the save stripped no empty herd.
* FAIL (SPORE_FIX_OFF=savestrip.our_herds): out/lab/20261113-045930 (fix/w49-tests-cpp worktree): 72 stripped, 52
  herds missing after the load.
* PASS: out/lab/20261113-045626 (0 stripped, 140 empty game herds kept, 0 missing).
BUG-0255 still holds (an empty herd made for a partner's puppet is stripped): save_hygiene_leave PASS, see History.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-tests (game::our_herd, savestrip.our_herds), twin test civ_save_herds.
- 2026-11-14: released in 0.7.14.
