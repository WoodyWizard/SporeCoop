---
id: BUG-9329
title: Lineages Cache: test creations of QA profiles are purged and banned (ban list shipped with the mod)
status: fixed
kind: feature
stage: all
reported: 2026-11-21
source: owner report 2026-11-21, decision 7Z A
role: all
versions: 0.7.14.1-lineages57-dev
fixed_in: unreleased
branch: fix/l47-ship-purge
commits: bbe18d12
tests: tools/qa_win (manual, TITAN QA games, 2026-11-21: coop.library, coop.library pickships 200, a save with a moved-out creature)
signature: 
docs: docs/re/creations.md#creations-ban-list
related: BUG-9222 BUG-9328
switch: library.banned
---

## Report
The owner (2026-11-21): the partner saw a test ship from their Lineages Cache in the sky; could the leftover test
creatures (the modified Linku ones) be purged from everyone's game, with a ban on creations of test players? Decision
7Z A: a ban list shipped with the mod, keyed by the test profiles' hidden Lineages identities and by single creations,
never by player name.

## Root cause
Not a bug in the code: test creations made before BUG-9222 (-NoShare-) were shared into real players' caches, imported
into their Sporepedia (Pollination) and from there picked by the game (visiting ships, NPCs), and passed on with Foreign
Creations.

## Fix
library.cpp `library.banned`, list data/lineages/banned.txt (released as mods/lineages/banned.txt by tools/package.sh,
tools/package_duo.py and the update pack, tools/mkpack.py; removed by uninstall.bat). Lines `source <identity>`,
`creation <sha256>`, `keep <sha256>`, comments with #. Content: the 35 hidden identities of every test profile on the two
test PCs (22 on TITAN, 13 more on the MSI, read only from the identity files) plus `manual-test` (console imports), 31
single test creations (modified test creatures, test ships, a test vehicle and a building), and 80 `keep` lines: real
creations that test profiles copied with a player's save (the same PNG also sits under test identities). One identity
seen in a test profile is not listed: it carries only a real player's copied creations and that player's sharer name.
At the first library step of a game start (the main menu, before a save loads) each listed received creation moves out
of the cache into LineagesCache\banned (never deleted; a line in banned\moved.txt) and out of the index; it is never
imported again, an offer of it counts as had (nothing is sent) and store refuses it, it is never offered, and the
object template DB's FindObjects (OTDB vt 0x24) drops its key, stamps it used and asks again, so the game never picks it
again. Its Pollination copy stays: saves that hold it load as before. The player's own creations (local entries, and
any key their EditorSaves answers for) are never moved or hidden. A creation that comes back from a source not on the
list is pickable again. `coop.fix library.banned off` (or fixes_off.txt): nothing banned. `coop.library` counters,
`coop.library pickships <n>` (tests).

## Verification
- FAIL (ban list present, switch off by fixes_off.txt; QA DuoQACreatureHost): nothing moved (imported 171), 12 of 200
  visiting-ship picks were banned test ships (2926CA3D..2926CA40, 292789F7).
- PASS (switch on, final build): 45 banned creations moved out at start (log "45 banned creation(s) moved out ..."; 45
  PNGs in LineagesCache\banned), imported 171 -> 126; 200 picks: 0 banned, 0 empty, 90 different ships;
  `coop.library import` of a moved-out test ship's PNG refused (ban_refused=1, nothing imported).
- A first version that only dropped banned results returned no ship in 200 of 200 picks (the query offers the templates
  used least lately, here the five banned ships): fixed before the final run (stamp and ask again).
- Saves: DuoQACreatureGuest Game20 Samuth, whose player species is a moved-out test creature (291D668C, the avatar and
  four of its species), loaded after its purge (34 moved) and played over a minute normally:
  out/qa-shots/bug9329_save_with_moved_out_creature_plays.png. DuoQACreatureHost Samuth, which references a moved-out
  test vehicle, loaded after its purge. A moved-out creature that lives only in Pollination still spawned with its
  model. Nothing is removed from the game's databases, so a save cannot lose a creation it holds.
- Not run live: the refusal of a banned creation offered by a partner and the never-sent side (test players always
  carry -NoShare-, which exchanges nothing); the release zip itself (only the update pack was built from a stub stage:
  it lists SporebinEP1/mods/lineages/banned.txt).
- no-fail-run: not a lab run (TITAN QA games by hand); no automated lab test was written or run.

## History
- 2026-11-21: opened.
- 2026-11-21: done on fix/l47-ship-purge (bbe18d12), FAIL / PASS live on TITAN's QA copy.
- 2026-11-21: decision 8C A (fix/l47-msi-ban): the test PC main profile's identity (ae4c8a7f...) stays off the list;
  its 81 creations (26 of them in test caches) were all made in 2020 in a real playthrough, none by a test game, so
  no creation line was added; a comment in banned.txt records the review (docs/re/creations.md#creations-ban-list).
