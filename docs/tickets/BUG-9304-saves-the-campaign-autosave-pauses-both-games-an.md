---
id: BUG-9304
title: Saves: the campaign autosave pauses both games and shows the saved box
status: open
kind: feature
stage: all
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-saves
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-entry-reloads, docs/re/creature.md#creature-save-steps
related: BUG-0332, BUG-9260, BUG-9137, BUG-9210
---

## Report
Live 67-dev: "Autosave isn't seamless still, this may have been solved on Alpha, but not on Live?"

## Root cause
Observed: every save on Live (and on alpha) is the pause menu's Save message (world.cpp ~571): a 1 s game-time pause,
the busy cursor, a ~0.7 s write and the saved box the mod clicks; guests pause with the host (BUG-9260). In Creature the
goal-card awards made 4 of them in 8 minutes (social.cpp host_goal_pay -> request_autosave). Alpha's silent save
(520ace7b) is used only for galaxy-server uploads, not for campaign autosaves. The two full reloads on entering Tribe
(BUG-0332 one ground; the founders' refresh) are a separate, structural cost (49.5 s loading screen for the guest).

Tribe entry, decision 7M B (observed in the code, 2026-11-20, fix/l47-saves; not traced at runtime):
* Reload 1: coop.cpp's stage change calls world::host_reload_after_share (`tribe.host_reload`, BUG-0332); the guests'
  share save goes out ~3 s into Tribe, each guest loads it, and the host loads the same save once the share left
  (world.cpp share tick, `g_reload_due`).
* Reload 2: tribe.cpp found_partners founds each partner's village only 15 s after the world was entered
  (founder_creature_settled, BUG-9137), i.e. after reload 1, and then founders_refresh -> world::checkpoint_reload
  saves, shares to every guest and loads the slot on the host: every game loads a second time.
* So the village could not be in the first share save: the founding waited for 15 s that, at a live entry, guard
  nothing (the partner's creature is the one it played in Creature, not one still coming with a new world; inferred).

## Fix
Two switches (`coop.fix <name> off`, or `SPORE_FIX_OFF=<name>` for the lab):
* `world.silent_autosave` (world.cpp autosave_tick): the campaign autosave is world::silent_save, ported unchanged from
  alpha 520ace7b (message 0x689C9B9 sent with a token object through MessageSend outside the frame;
  docs/re/creature.md#creature-save-steps): no game-time pause, no busy cursor, no saved box, so the guests do not
  pause either; only the write's frame (0.7-0.9 s on alpha) remains. It is confirmed by a newer file in the slot (it
  has no box); save_busy() is true for 4 s after it (as alpha's uploads wait 4 s). A silent autosave that wrote nothing
  in 30 s makes the next autosaves of that world the pause-menu save again (counted). Shares and checkpoint reloads
  keep the pause-menu save (their logic waits for its box). Off: the pause-menu save for the autosave too (0.7.14.1).
* `tribe.one_entry_reload` (tribe.cpp founders block, world.cpp set_entry_share_hold): while the live Creature ->
  Tribe entry's share is due (`g_reload_due`, the one the host reloads after), its save waits up to 20 s (from the
  share's request) while found_partners makes the partners' villages (checked every 1 s instead of 5 s; the 15 s
  after-entry rule does not apply then, the 10 s "creature unchanged" rule does). The share save then carries the
  villages and their `.duofounders` roster, the BUG-0332 reload is the only load, and founders_refresh skips its
  checkpoint reload. The hold ends when the founding is done or fails, when every partner has a seat, or when a
  partner waits for something long (no creature of their own, in the editor); a partner still settling keeps it. After
  20 s the share goes out and the old way follows (founding after the reload, its checkpoint reload). Only the
  multi-founder path (`tribe.multi_founders`, the default); `tribe.host_reload off` turns it off too. Off: the share at
  once, the founding later and its checkpoint reload (0.7.14.1).
* Counters: `coop.status` (world) "silent autosaves N, confirmed N, pause-menu fallbacks N; Tribe entry shares held
  for villages N (to the 20 s N)"; `coop.tribe` host "tribe founders: ... one_entry_reload=1 entry_foundings=N
  entry_releases=N". Logs: "autosave requested (silent save, BUG-9304)", "waits for the partners' villages of the new
  Tribe stage", "the Tribe entry's share goes out (BUG-9304): ...".

Not done / what could block the rest (inferred, not seen): the guest now waits in its old Creature world up to the
founding (about 3-12 s, at most 20 s) before its one load; a host whose game is paused or plays a cinematic right after
the entry (found_partners does not run then) falls back to the two reloads after 20 s. The host's own reload (BUG-0332)
stays: only a load gives everyone the same ground, so one load each is the floor without a terrain fix.

## Verification
Built only (2026-11-20, `tools/build_windows.sh coop`, no new warnings); no game was run. Planned two-player test
(host + guest QA games, GOG 2024 / Steam; commands through `tools/qa_win/qa.py <instance> <command>`):
* Autosave: in any stage with the guest in the world, host `coop.autosave-test` (QA instances only). PASS
  (`world.silent_autosave` on): no saved box, no busy cursor, the guest's "Paused by host" cover never shows, the
  host log has "autosave requested (silent save, BUG-9304)" then "campaign autosave confirmed"; host `coop.status`:
  "silent autosaves 1, confirmed 1". FAIL expected after `coop.fix world.silent_autosave off` and another
  `coop.autosave-test`: the saved box shows and is clicked, both games pause about 1 s, the counters stay.
* Tribe entry: a Creature campaign, the guest playing its own creature (docs/re/creature.md#creature-tribe-start-live):
  host `coop.dna brain 4`, `coop.dna add 3000`, `coop.vote tribe`; the guest's F9 passes the vote; the host's Done in
  the stage-end editor. PASS (`tribe.one_entry_reload` on): the host log has "waits for the partners' villages of the
  new Tribe stage" then "the Tribe entry's share goes out (BUG-9304): the villages are made"; the guest has one
  loading screen and its own village right after it; host `coop.tribe reload` host_reloads=1 and `coop.tribe`
  "entry_foundings=1"; no "Saving and refreshing the shared campaign" notice and no second loading screen on either
  game in the next 2 minutes. FAIL expected after `coop.fix tribe.one_entry_reload off` (restore the fixture first: the
  stage end saves over the host's slot): that notice and a second loading screen on both games about 15-30 s after
  the first, entry_foundings=0.

## History
- 2026-11-20: opened.
- 2026-11-20: fix/l47-saves: `world.silent_autosave` and `tribe.one_entry_reload` (decision 7M B); built, not run.
