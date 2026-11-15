---
id: BUG-0864
title: Feature idea: host campaign autosaves after milestones and every ten minutes
status: open
kind: feature
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages36-dev (autosave.h, world.cpp)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0804
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

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
