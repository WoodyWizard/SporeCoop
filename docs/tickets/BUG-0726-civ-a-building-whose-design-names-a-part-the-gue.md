---
id: BUG-0726
title: Civ: a building whose design names a part the guest lacks was left out of the guest's city (allow_copy false)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: guest
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content-b
commits: a10f59bd, dffd1a16
tests: civ_design_content, civ_design_content_slow (coop_civ_design_content.py)
signature:
docs: docs/re/creations.md#creations-k2b-design-content
related: BUG-0424, BUG-0469, BUG-0670
---

## Report
K2b audit of the class BUG-0424: civ.cpp building_model_ok checks a building's design with `ensure_model(key, kMkBuilding, allow_copy=false)`: a design whose model names a part this game lacks was refused and the host's building was not built on the guest at all (a gap in the host's city).

## Root cause
Observed (lab 20261108-032432, `SPORE_FIX_OFF=content.building_copy`, the guest made to lack one part of the host's house design): the host built the house, the guest's city never got it (no building of that design within 45 s). allow_copy=false was a rule of the first BUG-0469 version, whose copy took a key of the mod's own; since `content.model_key` the copy with the substitutes answers the model's OWN key above every package of the game, so cCity::SetBuildingModel reading the design key itself builds the copy.

## Fix
ensure_model builds the substituted copy for a caller that cannot pass another key whenever the copy answers the model's own key (`content.building_copy`, with `content.model_key`). `coop.fix content.building_copy off`: refused (0.7.10).

## Verification
FAIL: out/lab/20261108-032432 (`SPORE_FIX_OFF=content.building_copy`: no house on the guest). PASS: out/lab/20261108-032608 (civ_design_content and civ_design_content_slow: the house is built at once from the copy, then shows the host's design).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065507, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `content.building_copy`; FAIL and PASS runs above.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
- 2026-11-08: released in 0.7.11.
