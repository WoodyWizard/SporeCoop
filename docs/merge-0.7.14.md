# Upstream merge plan: SporeCoop 0.7.14.1 into Lineages (decision 4E A)

Prepared 2026-11-15 after Lineages 55-dev. Merged the same day on feat/lineages-merge-0.7.14 (9cb65a6e); the decisions taken are below.

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


## Decisions taken in the merge (2026-11-15)

Wire and ids (every peer of this build is the same build, so layouts may change):

* `PlayerFlags` became `uint16_t` on the wire: ours `PF_DOWNED = 128`, upstream's `PF_RESYNC` moved from 128 to 256.
* `PlayerState` tail: upstream's `body` right after `agl`, then ours (`captain_scale`, `energy`, optional `editor`,
  optional `gate`); tests/net_tests.cpp adjusted to the new offsets.
* Message ids: ours 65-69 collided with upstream's new 65-69; ours moved to MSG_CAMPAIGN_RULES 88, MSG_CELL_COMBAT 89,
  MSG_CIV_TURRETS 90, MSG_CIV_ECONOMY 91, MSG_CIV_INFLUENCE 92.
* player_rules kinds: ours K_SUMMON / K_RECRUIT (3, 4) collided with upstream's K_UFO / K_HOLD: ours are 11 and 12.
* creations bundle kinds: our kPortrait (6) collided with upstream's design kinds 6..8: kPortrait is 9.

Same feature on both sides (one path kept):

* Summon Flock of a guest: our guest pack (BUG-9129) wins; upstream's creature.flock_relay (BUG-0736) only runs when the
  guest pack is off.
* Partner creature size: ours (captain_scale, BUG-9113 / BUG-9206) drives it; upstream's creature.puppet_scale
  (BUG-0733) is kept but off by default.
* Game saved box: ours BUG-9057 / BUG-9101 folded into upstream's world.saved_box_only (BUG-0846), world.saved_box_cursor
  (BUG-0804) and world.saved_box_offscreen (BUG-0806); our hover before the click stays.
* Download retries: upstream's (BUG-0755) replace ours; our Lineages release feed parsing stays.
* Folder move retry: one `slotfs::move_staged` (upstream BUG-0848, 1.5 s) honouring both switches; ours (BUG-9028)
  lost its own COOP_FIX (the name world.staged_move was taken twice).
* creature.bar_spend: upstream's BUG-0630 switch also covers our BUG-9092 progress sync (one switch).
* Share collection: upstream's slotfs::collect (BUG-0847, complete reads) replaces our world.cpp collect; it skips our
  `.duo-tmp-` side-file temp copies.
* Virtual cursor: upstream's BUG-0804 (`vcursor_state(x, y, bool& virt)`, release after 2 frames) replaces our BUG-9101.
* uninstall.bat and validate.h: upstream's (supersets of ours).
* Rivals budgets: keyed by our identity key (BUG-9198) with upstream's newcomer rule (BUG-1029).
* Fleet: our leader model (BUG-9197) with upstream's shots (BUG-0677), friendly-fire and War rules (BUG-0420, BUG-1032).
* Session limit: our seat_limit and upstream's max_players / join_closed both apply (the smaller counts).
* The Windows toolchain has no `<condition_variable>`: sporekit/console.cpp polls instead.

More ids moved (every peer runs this build):

* civ commands: upstream keeps 5..8 (space build, BUYCITY, CAPTURE, specialty); ours CMD_WALL_EDIT 9, CMD_WALL_ACK 10,
  CMD_CITY_PURCHASE 11. The Civ vehicle snapshot is ours (45 bytes) plus upstream's top-speed tail (BUG-0787).
* tribe commands: ours keep their numbers; upstream's CMD_CONQUER (4) is 11.
* host_world.h `msg_class` (default deny, BUG-0420 class) now names every Lineages message: Session (identity, library,
  campaign rules, first creature, migration, progress debt, launch, rescue, outfit, adventure), HostWorld (Tribe and
  Civ rows, drama, Cell combat), Module (adventure actors).

The big files (merged by helpers, checked by the build):

* coop_ui.cpp: our one-page Co-op window (BUG-9117) stays; upstream's step window (0.7.12 F1, BUG-0765 name step) and
  its "More >" LAN paging are dropped (core.ui_name_first is gone). Kept from upstream inside our 760x600 window: player
  colours (F2), the session settings page (F3), the report round (F4), Evolve Now (BUG-0516), one-creature mode (F5),
  give-row sizing (BUG-1062), the Space picker (BUG-1050), the pause menu's "Report a problem". The new widgets'
  places need a screenshot check.
* coop.cpp: our evolution request (tag 0x64AA168) is marked by a call guard (core.lineages_evo_req) so upstream's stage
  end rule (BUG-1085) does not take our join, migration and mating editors for a stage end. tribe.graduation_save is
  one switch (ours BUG-9050 and upstream BUG-0849 are the same fix). One `coop.adventure` command with both sides'
  subcommands. Upstream's partner arrows are off while our chevrons (ui.partner_arrows) are on; F2 colours replace our
  gold / blue tribe tags.
* tribe.cpp: upstream's BUG-0612 / BUG-0610 / BUG-0844 replace our identical backports (BUG-9090 / 9091 / 9051, same
  switch names). Our BUG-9183 PlayCinematic log no longer hooks (world.cpp's BUG-0626 owns that function).
* civ.cpp: one hook of cCity::BuyByCiv (ours, BUG-9082); upstream's h_CityBuy (BUG-0877) runs through it. A partner's
  away capture (BUG-0878) goes to that partner's founder nation (untested).
* npc.cpp / cellsync.cpp: one hook of the cell damage getter (ours scales, upstream's BUG-0714 forced damage first).
  Cell meals now go through upstream's in-step food queue (BUG-0633), our earned_food inside it.

Was open (owner decision): upstream's Creature stage end (BUG-1085) is a host vote and a guest's own request is refused;
ours made every Creature editor personal. Which rule for the stage end?
Decided (owner 2026-11-15, ROADMAP 4F A): the stage end stays the host's vote for the whole party; every other
Creature editor stays personal. This is the merged behaviour, no code change.
Next (ROADMAP 4G): when to release the merge as 56-dev.
