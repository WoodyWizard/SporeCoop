---
id: BUG-9252
title: Guest joining from a stage stays on the joining screen: the first-creature editor waits for galaxy intro bars that never go
status: fixed
kind: freeze
stage: Creature
reported: 2026-11-17
source: QA (galaxy hand-over test, 2026-11-17)
role: guest
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages65-dev
branch: feat/lineages-47
commits: 584c59ea
tests: manual Windows QA with tools/qa_win/qa.py (TITAN, local galaxy server, QA games 9 and 8; Samuth.spo)
signature:
docs: docs/12-server-and-players.md#server-world-storage, docs/known-issues.md#issue-pregame-editor-overlays
related: BUG-9208, BUG-9209, BUG-0626, BUG-0938
---

## Report
Galaxy hand-over test (alpha, TITAN): QA game 9 played its own Creature world, joined game 8 hosting the same planet
(from the galaxy server's stored copy) as a guest and stayed on the galaxy menu's joining screen ("Player is saving the
world...") for 5 minutes: the host's world arrived twice and never loaded.

## Root cause
Observed: game 9 had no creature of its own in that world (the new host's creature had become the world's player
species), so the world was held for the first-creature editor (BUG-9208: "it waits for our first creature"). The
editor waits while Spore's cinematic bars (letterbox_top / letterbox_bottom) are up (BUG-9209). The guest had reached
the galaxy menu by leaving its stage, the menu's intro was not started (BUG-0626: "would start outside the menu"), and
the bars stayed up with no cinematic playing. The hold's own 120 s limit is checked after the bars' wait, so it never
fired. Switching `creature.editor_after_intro` off let the 120 s limit release the world, and the guest loaded it.
Inferred: any guest that leaves a stage to load a host's world where it has no creature can hit it, not only the
galaxy.

## Fix
coop.cpp: bars up for 5 s with no cinematic playing (world::cinematic_playing / menu_intro_playing) are stale and
hidden; the editor opens on a later tick. The 120 s limit of the held world also counts while the bars are waited for.
Switch `creature.stale_intro_bars` (off: the old wait).

## Verification
no-fail-run: reproduced by hand on Windows QA games, not in the lab (the galaxy server is alpha only). Without the
fix (alpha build before 584c59ea): game 9 stayed on the joining screen from 16:35:44 to 16:40, released only by
switching the bars' wait off. With it: "the intro bars were up 5 s with no cinematic: hidden (BUG-9252)" 5 s after the
hold, then the creature editor opened (mode Editor). Not fixed here: the loading screen over that editor and the menu
splash text (docs/known-issues.md#issue-pregame-editor-overlays).

## History
- 2026-11-17: opened, fixed on feat/lineages-47 (584c59ea), unreleased.
