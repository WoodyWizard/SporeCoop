---
id: BUG-0864
title: Feature idea: host campaign autosaves after milestones and every ten minutes
status: fixed
kind: feature
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages36-dev (autosave.h, world.cpp)
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-store
commits: a3f08970
tests: autosave_milestone, autosave_timer, store_tests (make test)
signature:
docs: docs/01-architecture.md#arch-autosave
related: BUG-0804, BUG-1149
switch: world.autosave
---

## Report
Players lose progress after a crash or a lost connection because nothing saves the shared campaign unless a
guest joins or the host saves by hand. The fork saves the host's campaign after milestones and every ten minutes.

## Root cause
Not a bug: we have no periodic or milestone save (grep "autosave" in mods/coop: none, 0.7.11.1). Our saves come
from world shares (world.cpp save_game) and evolution saves (BUG-0517).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages36-dev, mods/coop/autosave.h and world.cpp autosave_tick); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* `AutosaveClock` {interval 600000 ms, since, event_pending}: due when an event is pending or 10 min passed.
* Milestones call `world::request_autosave()` (event_pending, first try 3 s later so an existing share save goes
  first): a new stage in the same world (coop.cpp on_mode_change), and an editor exit whose EditorSaves.package
  write time is newer than on entry (creations.cpp adopt_after_editor, checked every 0.5 s for 10 s).
* autosave_tick (host, connected, playing): skip while save_busy / save_pending / a load queued / a cinematic /
  time paused / a checkpoint reload / a Cell rescue pending; retry every 5 s. Any save_game (also a share save)
  counts as the autosave. Confirmed only when the saved box was seen after the post and the slot's newest file
  write is newer than before; else after 30 s it re-arms with a 15 s delay. Feed "Campaign autosaved".
  ini `coop.autosave=1`.
* Overlap: plan-0.7.12 F3 (a session setting "autosave every N min" would fit there). Depends on BUG-0804 (the
  mouse after the saved box) or every autosave takes the mouse away for 5 s.

## How to test
Lab, 2 instances: host plays with a guest; set the interval to 60 s by a test switch; check a new file write in
the slot and the feed line, the simulation never saved during a cinematic; a stage change triggers a save within
5 s; the guest's mouse and avatar are not affected.

## Fix
E2 of docs/plan-0.7.15.md: while it hosts, the host's game saves itself (world::save_game, its own save) every N minutes
(session setting all.autosave: off / 5 / 10 / 15 / 30 / 60 min, default 10, Live) and 5 s after a milestone (a later
stage, a colony founded, a city captured, an epic killed); any save restarts the timer; outside the frame; waits for
cinematics, a share's save and the save box, save_game refuses editors, loads, mode switches and Space on a planet.
autosave.h (clock, unit tested), autosave.cpp (`coop.autosave`). The co-op window's session line: "saved N min ago".
`coop.fix world.autosave off`: no autosave.

## Verification
coop_autosave.py: the host's money +12345, the autosave (a guest's colony = milestone; --timer: 40 s), the host's
game killed (session_restart(save=False)), its save loaded again.
* autosave_milestone: FAIL (SPORE_FIX_OFF=world.autosave): out/lab/20261115-130149 (money 100000, not 112345); PASS:
  out/lab/20261115-130030, out/lab/20261115-131036 (with the F9 line key frame), out/lab/20261115-135138
* autosave_timer: FAIL (SPORE_FIX_OFF=world.autosave): out/lab/20261115-130245; PASS: out/lab/20261115-130050,
  out/lab/20261115-133522, out/lab/20261115-135226
* unit: store_tests test_autosave (timer, milestone delay, retries, lost saves, off).

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
- 2026-11-15: done on feat/w51-store (E2).
