---
id: BUG-0725
title: Civ: a host vehicle or building design with a part the guest lacks got a fixed fallback part, not the most similar one (part descriptions dropped)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-b
commits: a10f59bd, dffd1a16
tests: civ_design_content, civ_design_content_slow (coop_civ_design_content.py)
signature:
docs: docs/re/creations.md#creations-k2b-design-content
related: BUG-0424, BUG-0559, BUG-0647, BUG-0670
---

## Report
K2b audit of the class BUG-0424: the lookalike of BUG-0559 (a part this game lacks shown as the most similar part it has, from the sender's description of the part) ran only for the bundles of a player's creature, cell and UFO. The host's vehicle, building and ship designs went out without their parts' descriptions (add_part_descs knew the three creation types only), apply_own_design dropped what came, and ensure_model called substitute_parts without descriptions: such a part became one fixed base game part of that kind (content_packs.h kBasePartFallbacks).

## Root cause
Observed (lab 20261108-032319, `SPORE_FIX_OFF=content.design_lookalike`; the guest made to lack the first part 8D53A9DF of the host's vehicle design and D86E4605 of its house design with `coop.creations lack`): no lookalike was chosen, the guest's mirror vehicle showed the fixed fallback 00046BAE in that part's place, and the house showed none of the host's design's lookalike either.

## Fix
One shared change in creations.cpp: every bundle carries its parts' descriptions (add_part_descs knows the part group of every model kind), every received description is kept for the session (`g_rx_descs`, by part id, from creature / cell / UFO bundles and the host's design answers), and ensure_model, the one check of every model the mod makes this game build, substitutes with them (the lookalike of the same role, abilities and size), the fixed fallback only for a part nobody described. `coop.fix content.design_lookalike off`: the fallback for the designs (0.7.10). Test hooks: `coop.creations lack <id>` (the mod treats a part this game has as one it lacks: one lab lane has one install for both games), `mparts <kind> <inst>`, `looks`.

## Verification
FAIL: out/lab/20261108-032319 (`SPORE_FIX_OFF=content.design_lookalike`: the fallback 00046BAE in the mirror, no lookalike). PASS: out/lab/20261108-032608 (civ_design_content and civ_design_content_slow at 20 fps over the player network: the mirror shows AB10C9B7, the lookalike the guest chose from the host's description of 8D53A9DF; the house is re-keyed to the host's design and shows the lookalike DD4EAF81 of D86E4605). Not shown on one lane: the bake fault of a part a game really lacks (the pack-moving tests content_civ / content_lookalike cover it).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065355, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `content.design_lookalike`; FAIL and PASS runs above.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
