---
id: BUG-9353
title: Galaxy: a Civ era's building in a Tribe game is solid only 1 m wide
status: wontfix
kind: bug
stage: tribe
reported: 2026-11-23
source: QA (MSI)
role: host
versions: 0.7.14.1-lineages69-dev
fixed_in:
branch:
commits:
tests: tools/qa_win (manual, 2026-11-23): the MSI's Tribe game on Clyth with Clyth's Civ era hosted
signature:
docs: docs/12-server-and-players.md#server-era-visitors-solid
related: BUG-9293, BUG-9290
---

## Report
Cross-PC QA (2026-11-23): 30 s after a Tribe game entered Clyth (Tribe) while Clyth's Civ era was hosted, `coop.visit
status` gave the Civ City Hall drawn on the Tribe village a box of (-0.5 -0.5 0.0)..(0.5 0.5 1.0): solid only 1 m wide.

## Root cause
Not a bug: the prop's box is the spatial object's default until its model has loaded. Minutes later the same City Hall
had its real box (-5.4 -6.3 -0.2)..(6.2 5.6 6.9); the rover's box was real from the start.

## Fix
None. A fix that sent each object's box from its own game (a VISITOBJ trailer) was written and built, then reverted
before commit once the real box was seen (no code for a problem that is not there).

## Verification
no-fail-run: not a bug (the box became real by itself; seen in `coop.visit status`).

## History
- 2026-11-23: opened.
- 2026-11-23: closed as not a bug (the box was the default until the model loaded).
