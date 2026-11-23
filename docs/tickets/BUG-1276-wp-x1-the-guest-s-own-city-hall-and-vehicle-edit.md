---
id: BUG-1276
title: WP-X1: the guest's own City Hall and vehicle editors at the Tribe -> Civ arrival
status: fixed
kind: feature
stage: Civ
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in: 0.7.16
branch: feat/w54-x1
commits: 176cc77e a083aaee f00246eb 800a17e6 39aebbea 42fc4f09
tests: civ_own_arrival, civ_own_arrival_off (twin)
signature:
docs: docs/re/civ.md#civ-own-arrival, docs/06-testing.md#testing-civ-own-arrival, docs/01-architecture.md#arch-civ-own-arrival
related: BUG-1240, BUG-1158, BUG-1263, BUG-1275, BUG-1274, BUG-1273, BUG-1277, BUG-1280
switch: core.own_civ_arrival
---

## Report
Plan item (docs/plan-0.7.16.md item 4, WP-X1): at the Tribe -> Civ change only the host goes through the game's City
Hall and vehicle editors (TRG2CVG_PreEditor, each after the host's vote); a guest's game follows into the host's Civ
world with the host's designs, and BUG-1158 refused its own design editors there. The owner's goal: every player its
own look in every stage, as BUG-1240 gave each player its own Creature editor at Cell -> Creature.

## Root cause
By design of the game (Ghidra, docs/re/civ.md#civ-own-arrival): the two editors are requested by the Tribe stage's
listener 0xCD1D60 at the end of two cinematics that only the host's game plays (TribeGame_AdvanceProc on the host);
the guest's game never runs the end of Tribe, it loads the host's Civ world. An editor's result becomes a culture entry
only through the planner's apply 0xD095A0 (WP-C3 skips it for a guest); a result of a request that does not come from
the planner is ignored by cGameModeCiv (tags 0x66787BB / 0x116D51D only) and re-opens no planner (+0xF8 = -1).

## Fix
`core.own_civ_arrival` (mods/coop/civ_arrival.cpp; session_glue.cpp's vote end, civ_editors.cpp's take_editor and the
result hook): in own mode (creature.mode own, species.line, civ.guest_editors, a host with creations 1.1) a guest that
sees the host's end-of-Tribe editor vote end while it plays the host's Tribe keeps its own Civ editors due; once it
plays the host's Civ world and has settled (20 s without a cinematic, pause, load, vote, pause menu; at most 240 s
after the arrival, 900 s after the vote) the game's City Hall editor 99E92F05 opens on its game (the Tribe listener's
request with the red cross, submitted outside the frame under the vote bypass), then the archetype's vehicle editor
(F670AA43 / 9AD7D4AA / 7D433FAD). Their results go to its species line (civedit::take_editor -> species::set_design),
never into the culture set; the planner's copy of the result is emptied. The host is told ("<guest> makes its own
City Hall and vehicle") and never waits: no vote, no hold. Opened by itself rather than offered: the request is the
game's own, in a mode whose return path was read (no planner re-open, no outfitter branch). Off: as 0.7.15.

## Verification
Owner rule of 2026-11-18: no lab runs until the night run. Test `civ_own_arrival` (PASS expected) and its twin
`civ_own_arrival_off` (SPORE_FIX_OFF=core.own_civ_arrival, FAIL expected at "the guest's OWN City Hall editor opened
after its arrival in Civ"), lanes 9 and 10. Run dirs: pending (night run). `make -j8`, `make test` pass.
Lab (fix/w57-own-editors, merged trees): FAIL before: rel/0.7.16 out/lab/20261119-095414 civ_own_arrival (no own
editor: BUG-1306), fix/w57-own-editors worktree out/lab/20261119-113002 (the vehicle editor never came: BUG-1323). PASS whole on the merged tree with
fix/w57-civlook: fix/w57-own-editors worktree out/lab/20261119-153243 civ_own_arrival (both own editors 22 s / 5 s after the arrival and its City Hall
editor's return, both designs one shared key on both games, the host's culture set unchanged, the guest's city's City
Hall, citizens and bought vehicle its own on both games, no guest key in the host's save). Twin civ_own_arrival_off
FAIL fix/w57-own-editors worktree out/lab/20261119-130049 ("the guest's OWN City Hall editor opened ... phase none").

## History
- 2026-11-18: opened.
- 2026-11-18: static RE, code, test and twin on feat/w54-x1; lab pending (night run).
- 2026-11-19: fix/w57-own-editors: civ_own_arrival: own City Hall and vehicle editors at the arrival PASS on the merged tree (fix/w57-own-editors worktree out/lab/20261119-142554: both opened, both designs one shared key on both games, the guest's city's City Hall and its bought vehicle show them on both games) after BUG-1306 and BUG-1323; the test buys in the nearest NPC city of the editor's land design now (a guest may not buy in the capital). The only FAIL left is the host save's design line keys (fix/w57-civlook, BUG-1274). Twin civ_own_arrival_off FAIL in -130049. Open until that save strip is in.
- 2026-11-19: fix/w57-own-editors: PASS whole on the merged tree (20261119-153243), twin FAIL (20261119-130049): fixed.
- 2026-11-23: released in 0.7.16.
