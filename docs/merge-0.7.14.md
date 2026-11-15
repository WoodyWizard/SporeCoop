# Upstream merge plan: SporeCoop 0.7.14.1 into Lineages (decision 4E A)

Prepared 2026-11-15 after Lineages 55-dev. Nothing is merged yet.

* Ours: `feat/lineages-47` at the 55-dev release (tag `v0.7.8-lineages55-dev`).
* Upstream: WoodyWizard/SporeCoop `main` 74611a57 "Release 0.7.14.1" (1502 commits since our base 0.7.8), kept locally
  as `refs/remotes/woody/main` (fetched read-only from `origin`; never push to `origin`).
* Merge branch: `feat/lineages-merge-0.7.14`, created at the 55-dev release. It replaces `feat/lineages-47` only after a
  full TITAN and MSI pass in every stage.

## Rules (from 4E A)

* File by file, keeping both sides' features. Where they fight in our scope, ours wins: the Party UI, Interface Size,
  the first creature (pre-game editor, start parts), Lineages Cache, nests per player.
* Network message ids (`mods/coop/net/protocol.h`) will collide: renumber ours above upstream's highest, keep both.
* Tickets do not collide (ours 9000+, upstream under 1100). Union-merged files (CHANGELOG, known issues, docs/re) need a
  check for interleaved text afterwards.
* Upstream's BUG-0623 (one DestroyInstance hook) is already ported (BUG-9214): take upstream's side there.
* Build after each large file, `make test`-equivalent lints (`tools/re_lint.py`, `tools/docs_lint.py`,
  `tools/tickets.py check`), then the Windows QA stage tour (Cell, Creature, Tribe, Civ, Space; TITAN host + MSI guest).

## Conflicts measured 2026-11-15 (trial merge in a scratch worktree, then aborted)

58 files. Lines inside conflict blocks, number of blocks, file:

```
2428 28 mods/coop/tribe.cpp
1359 27 mods/coop/coop_ui.cpp
1253 30 mods/coop/coop.cpp
688 30 mods/coop/civ.cpp
358 15 mods/coop/npc.cpp
342 14 mods/coop/cellsync.cpp
325 2 data/symbols/overrides_dc04aee5.sym
315 8 mods/coop/creations.cpp
303 6 tests/net_tests.cpp
261 7 mods/coop/squadron.cpp
254 2 mods/coop/gamefixes.cpp
182 7 mods/coop/modes.cpp
177 4 mods/coop/creature_hp.cpp
174 8 mods/coop/world.cpp
127 1 data/symbols/frozen_off_922af7c7.txt
122 4 mods/coop/sui.cpp
120 4 mods/coop/player_rules.cpp
88 2 tools/tests/coop_ui.py
84 1 mods/coop/worldobj.cpp
81 2 docs/tickets/BUG-0177-creature-the-mating-call-did-not-lead-to-the-edi.md
81 1 installer/uninstall.bat
66 4 sporekit/commands.cpp
64 4 updater/winupdate.cpp
58 3 mods/coop/game.cpp
51 1 mods/coop/net/validate.h
49 2 sporekit/focus.cpp
40 4 mods/coop/autoupdate.cpp
39 2 mods/coop/world.h
36 2 mods/coop/space_allies.cpp
35 3 Makefile
33 3 mods/coop/net/protocol.h
33 2 mods/coop/drama.cpp
32 3 mods/coop/net/session.h
31 3 tests/win_tests.cpp
29 2 mods/coop/space_rivalry.cpp
28 2 sporekit/core.h
27 2 sporekit/input.cpp
26 3 installer/setup.cpp
25 4 mods/coop/net/session.cpp
25 2 mods/coop/effects.cpp
24 1 tools/tests/coop_ui_status.py
24 1 mods/coop/mouse.cpp
23 2 mods/coop/game.h
17 1 updater/winupdate.h
16 1 mods/coop/slotfs.h
16 1 mods/coop/net/fixes.h
14 1 mods/coop/cellsync.h
13 2 mods/coop/slotfs.cpp
13 2 mods/coop/gamefixes.h
9 1 mods/coop/space_map.cpp
7 1 tools/tests/suite.py
7 1 sporekit/d3d9hook.cpp
6 1 mods/coop/teamplay.cpp
6 1 mods/coop/social.h
6 1 mods/coop/shared.h
5 1 mods/coop/spacecoop.cpp
5 1 mods/coop/nest.cpp
5 1 mods/coop/debug.cpp
```

## Suggested order

1. Small files first (under 60 lines each): they show the shape of upstream's changes cheaply.
2. `overrides_dc04aee5.sym`, `frozen_off_922af7c7.txt`: keep every line of both sides, then `re_lint`.
3. `protocol.h` ids, then `world.cpp`, `creations.cpp`, `cellsync.cpp`, `npc.cpp`, `creature_hp.cpp`, `modes.cpp`.
4. `civ.cpp`, `squadron.cpp`, `gamefixes.cpp`.
5. The big three last, one at a time with a build after each: `coop.cpp`, `coop_ui.cpp`, `tribe.cpp`.
6. Full stage tour on both PCs, then a release candidate.

Effort: several sessions; the big three are most of it.
