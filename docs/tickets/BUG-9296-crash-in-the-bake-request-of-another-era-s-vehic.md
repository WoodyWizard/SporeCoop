---
id: BUG-9296
title: Crash in the bake request of another era's vehicle at a Tribe era's start (era_visitors baked_or_ask)
status: fixed
kind: crash
stage: tribe
reported: 2026-11-20
source: TITAN QA run 2026-11-20 (crash-20261120-195419-i7)
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 4dd70c23
tests: in game through tools/qa_win/qa.py (QA 7 entering Abber (Tribe) while QA 8 hosts Abber (Civ))
signature: Editors::cEditor::HandleMessage+0x11E; CastTo_cEditorResource_+0x351 (in baked_or_ask)
docs: docs/re/creations.md#creations-bake-in-tribe
related: BUG-9290, BUG-9291
---

## Report
A Tribe game entering Abber (Tribe) while Abber's Civ era streamed its buildings and vehicles crashed within
6 s, three times (crash-20261120-195419-i7, -195916-i7, -200250-i7, -200711-i7).

## Root cause
Observed: (1) the bake ask in the frame (IBakeManager vt 0x4C from era_visitors baked_or_ask, inside on_frame)
crashed at once in CastTo_cEditorResource_ (twice, the second with `galaxy.bake_outside_frame` off). (2) Asked outside the
frame, the game crashed ~5 s later in Editors::cEditor::HandleMessage+0x11E with a null (the render thread), once after a
second ask of the same key and once after a single ask of each key. Inferred: the bake's end reports to an editor the
Tribe stage does not have. The Creature stage bakes the same models without trouble (BUG-9291 runs).

## Fix
Switch `galaxy.bake_outside_frame` (on): a bake is asked outside the frame (SkHost::run_outside_frame), never again
while IBakeManager vt 0x18 says it is asked and not done, and not at all outside the Creature stage (there the prop is made
with what is baked). Off: as before (asked in the frame, every 5 s).

## Verification
TITAN QA 7 (DuoQAClythHost, Clyth.spo) entering Abber (Tribe) with QA 8 hosting Abber (Civ), 2026-11-20:
- FAIL (switch off): crashed 0.4 s after hosting (crash-20261120-195916-i7, in baked_or_ask's in-frame ask).
- Outside the frame only: crashed after ~5 s (crash-20261120-200250-i7, -200711-i7).
- PASS (the fix as built): hosted Abber (Tribe) for 30+ minutes over several runs, no crash; 9 props made with nothing asked.
- Not seen: whether a prop with nothing baked draws in Tribe (no Civ object was on the Tribe player's screen).

## History
- 2026-11-20: opened.
- 2026-11-20: found and fixed (crash only when a Tribe game shows a Civ era's objects).
