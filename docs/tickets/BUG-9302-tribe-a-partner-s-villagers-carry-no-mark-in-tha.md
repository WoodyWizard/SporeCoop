---
id: BUG-9302
title: Tribe: a partner's villagers carry no mark in that player's colour
status: open
kind: feature
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: all
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9282
---

## Report
Live 67-dev, Tribe: "Party's creatures were not highlighted the party color, so they were hard to tell who was who."

## Root cause
Feature gap: BUG-9282 colours only the partner's name plate; nothing marks tribe members on Live or alpha. Village
identity colours (cTribe +0x234) are the game's own forming-order colours and tint only huts and flags.

## Fix
Decision 7K (how to mark them). The owner of each member is known (g_gm[id].owner on the guest, g_hm on the host) and
the colour is find.h color_of.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
