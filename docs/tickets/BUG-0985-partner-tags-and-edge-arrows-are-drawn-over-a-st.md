---
id: BUG-0985
title: Partner tags and edge arrows are drawn over a stage cutscene (the host in the Creature stage intro, campaign advance-cell)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 (key-frame review)
role: 
versions: 0.7.13-rc
fixed_in: unreleased
branch: fix/w48-fui
commits: 7fc88a7e dd572e4b
tests: tag_cinematic
signature:
docs: docs/06-testing.md#testing-tag-cinematic
related: BUG-0949, BUG-0924
---

## Report
Key-frame review of the rel/0.7.13 final check (lab out/lab/20261112-175447, sheets 002 campaign, 012 campaign_ct,
017 campaign_short, all advance-cell): the host stood in the Creature stage's intro ("It is the beginning of a new
day ... The race to evolve has begun", busy cinematic,paused) 20 s after the stage change while the guests played,
and the partners' name tags (Carol, Bob, Dave) were drawn at the left edge over the cutscene.

## Root cause
Observed: the overlay (coop.cpp on_overlay) hid the world labels (tags, edge arrows, pings) only while the pause menu
or the co-op window was up; a cinematic was not a reason. A stage intro moves the camera away from the avatar, so
the partners' tags and arrows stand over the cutscene's picture. Player-visible on every stage change into Creature
for the player who is still in the intro (and in every other cutscene that holds the game: the nest claim, Tribe and
Civ captions).
The host standing in the intro 20 s later is the game waiting for the player's click on the caption's check button
(the campaign bot answers it later): not a bug.

## Fix
mods/coop/coop.cpp: the world labels stay off while a cinematic plays with the input locked or the time stopped
(world::cinematic_playing, coop_ui::input_lock_now, world::time_paused) in Cell, Creature, Tribe and Civ. Space and
the galaxy keep their labels (their flights play as cinematics with the input locked: 139 trace lines `Space
cinematic,input_lock` in the final check; the arrows are how players find each other there). The minimap blips are
not touched. `coop.status` counts the frames (`labels: ... cinematic_frames=N`).
Switch: `coop.fix find.cinematic off` (the labels over every cinematic, 0.7.12).

## Verification
Test `tag_cinematic` (coop_tag_cinematic.py): the guest's game plays CRG_ClaimNest through the game's PlayCinematic
(busy cinematic,input_lock); the overlay's last pass (`coop.find boxes`) and a key frame.
FAIL (SPORE_FIX_OFF=find.cinematic): out/lab/20261112-212634 (during the cutscene: ['Alice'] drawn).
PASS: out/lab/20261112-212752 (before ['Alice'], during [], after ['Alice']; the key frame shows the letterboxed
cutscene with no tag).

## History
- 2026-11-12: opened.
- 2026-11-12: fixed (find.cinematic), test tag_cinematic; fix/w48-fui.
