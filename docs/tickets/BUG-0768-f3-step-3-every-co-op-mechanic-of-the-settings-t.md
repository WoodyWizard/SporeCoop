---
id: BUG-0768
title: F3 step 3: every co-op mechanic of the settings table is a session setting
status: fixed
kind: feature
stage: all
reported: 2026-11-08
source: plan-0.7.12 F3 (Discord suggestions 2026-10-31..11-04)
role: all
versions: 0.7.11
fixed_in: 0.7.12
branch: feat/w46-settings-all
commits: 6efaacb0, d36b281b, b589db32, 7c689797, 1a87b3db
tests: settings_cell, settings_creature, settings_tribe, settings_civ, settings_space, switch_vote_off, editor_now_off, tribe_duet_off, ui, net_tests test_join_rules
signature:
docs: docs/07-modes.md#modes-session-settings
related: BUG-0762, BUG-0765, BUG-0315, BUG-0516, BUG-0899
---

## Report
F3 of docs/plan-0.7.12.md (approved 2026-11-08): every co-op mechanic of the settings table is a host setting, default
= the 0.7.11 behaviour. Steps 1-2 (BUG-0762, BUG-0764) wired cell.regen, all.partner_arrows, all.partner_map, dna_pace,
npcs_target_partners and the three teamplay rows.

## Root cause
Not a bug: the mechanics were always on (or an ini line), nothing let the host switch them per session.

## Fix
23 rows more in settings_core.h kDefs (keys, groups, help, defaults, scopes; named values for creature.mode, BUG-0769),
each read where its mechanic acts (the table in docs/07-modes.md#modes-session-settings): all.name_tags (on_overlay),
all.friendly_fire (Creature: npc.cpp damage_filter, MSG_DAMAGE to and through the host; Cell / Tribe / Civ: BUG-0899),
all.max_players and all.late_join (net/session.cpp HELLO: REJ_FULL with the limit, REJ_STARTED = 4; a player seen in
the session or with its rejoin token always comes back), cell.knockout / cell.respawn_small (go_down, downed_tick: the
game's own death), core.switch_vote / core.editor_now (the 0.7.11 E1 switches as session settings), tribe.pincer /
chief_aura / duet / epic, civ.roles (role()) / joint_siege / superweapon_vote (h_FireSW and the guest's request fire at
once), space.disable and the Space features (space_ext.cpp: no tick, messages, overlay, page rows; finale h_Enter,
diplomacy sharing()). The co-op window lists 31 rows on 3 pages (13 a page); coop_ui.py pages to its settings.
Twin switch for all of them: `coop.fix settings.all off` (SPORE_FIX_OFF=settings.all): every row from all.name_tags
on reads its built-in default.
Test commands: `coop.team pvp <id> <dmg>`, `coop.tribe epic force`; counters: coop.status `name_tags=`, `npc friendly
fire: ...`, coop.pool `ko_respawns= ko_deaths= dying= dead=`, coop.civ `sw_unvoted=`, coop.space `disable_off=`.

## Verification
Lanes 6/7, GOG 2024, SPORE_DETECTOR=20, --retry 0 (FAIL runs: SPORE_FIX_OFF=settings.all):
* settings_tribe: PASS out/lab/20261110-184431, FAIL out/lab/20261110-184633
* settings_cell, settings_creature: PASS out/lab/20261110-185222, out/lab/20261110-185222-2; FAIL out/lab/20261110-185421
* settings_civ: PASS out/lab/20261110-185422; FAIL out/lab/20261110-190750-2
* settings_space, editor_now_off, tribe_duet_off: PASS out/lab/20261110-190105-2; FAIL settings_space and
  tribe_duet_off out/lab/20261110-190750, editor_now_off out/lab/20261110-190750-2
* switch_vote_off: PASS out/lab/20261110-190105-3; FAIL out/lab/20261110-190750-2
* ui (coop_ui.py, lane 2, the settings pages): PASS out/lab/20261110-190105
* after the merge of rel/0.7.12 (0.7.11.2): settings_cell, settings_creature, settings_tribe, settings_civ PASS
  out/lab/20261110-191432
* the default behaviour kept (the tests of the switches that became settings): switch_vote, editor_now, tribe_duet,
  settings PASS out/lab/20261110-191616
* the player limit and late join with a third player: tests/net_tests.cpp test_join_rules (make test)

## History
- 2026-11-08: opened.
- 2026-11-10: wired, tested on lanes 6/7 (PASS and FAIL runs below), fixed (unreleased).
- 2026-11-11: released in 0.7.12.
