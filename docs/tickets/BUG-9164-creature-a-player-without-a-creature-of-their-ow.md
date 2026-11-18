---
id: BUG-9164
title: Creature: a player without a creature of their own goes into their own editor after joining (decision 4, as Tribe 2M)
status: open
kind: feature
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + 2 guests 2026-11-13: a fresh profile joins)
signature:
docs: docs/re/creature.md#creature-join-editor-first
related: BUG-9159 BUG-9160 BUG-0345 BUG-0508
switch: creature.join_editor_first
---

## Report
Owner test 2026-11-12: after the host evolved, a guest "became the new creature instead of their own (which I don't think is added yet)". A guest without a creature of its own plays the world's species, so every host evolution changes it (BUG-0508's evolve-in-place). Roadmap decision 4: each player has a creature of their own; Tribe already sends a joiner into its own editor first (2M, BUG-9159).

## Root cause
Design. creations::plays_own_creature() is false for a guest that plays the host's species, one of the host's creations or "the world's own". The creature stage's own evolution request (submit_evolution_request, BUG-9163) opens the guest's personal editor (BUG-9160) on the world's species; what it saves is recorded as its own creature for this world (worlds.ini [creature], BUG-0345), so the home nests give it a nest of its own.

## Fix
creature.join_editor_first (on). coop.cpp join_editor_tick: a connected guest in Creature, 12 s after its creature appeared, once per world, not busy, down or dead, without a creature of its own: its own evolution editor (outside the frame), a feed line ("Make a creature of your own ...") and a notice to all. Editors::EditorRequest::Submit returned false although the editor opened (seen live), so only -1 (not possible here) counts as a failure.

## Verification
no-fail-run: a feature. Live 2026-11-13, TITAN host + guest with its own creature + a third game on a fresh profile: the guest with its own creature got no editor (correct); the fresh joiner was in its editor 21 s after the join command (on the host's species), added a part, Done: "own creature 2920802E recorded for world", "Joiner plays its own creature", the host made seat 3's nest ("home nests: seat 3 (Joiner) has its own nest of 7CE29548") and sent the world again; after that load the joiner's avatar was its own species again ("own avatar is now 2920802E"); the host and the other guest saw the joiner in the cocoon while it edited (coop.evolve partners: cocoon 2). Found on the way: BUG-9165 (the reload took the joiner's own DNA). The game's first-run account box shows over the stage on a fresh profile (closed by hand).

Cross-PC 2026-11-13 (the MSI joining the TITAN host over Tailscale): its own editor opened about 25 s after the join (Submit -> 1 there); the host and a TITAN guest showed it in the cocoon (coop.evolve partners: cocoon 2); its saved creature got its own nest and the world.

## History
- 2026-11-13: opened.
