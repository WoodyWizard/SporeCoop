---
id: BUG-9149
title: downed players wait a minute for a revive on every stage
status: fixed
kind: feature
stage: all
reported: 2026-11-12
source: owner request 2026-11-12
role: all
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 99b846b6
tests: tools/qa_win (manual QA 2026-11-12)
signature:
docs:
related: BUG-9118 BUG-9141
switch: core.down_60
---

## Report
Owner: "Let's increase the 30s downed timer to 1m. It takes time for players to reach their teammate to even attempt a revive."

## Root cause
A request. The stages with a downed timer: Cell (cellsync down_time), Creature / Adventure (creature_rescue window_s), Tribe (tribe down_time), Space (spacecoop down_time: a disabled ship).

## Fix
down_time.h `downed::seconds`: 60 s on every stage (`coop.fix core.down_60 off`: each stage's `*.down_30` as before). Space also gets the ring (BUG-9141): its own disabled ship and a disabled partner's ship; the "REPAIR <name>" beacon loses its seconds while the ring is on.

## Verification
no-fail-run: a timing value. QA 2026-11-12, Creature, four players: "MSI is down! Stand next to them within 60 s." then "MSI was revived." by another guest (BUG-9146).

## History
- 2026-11-12: opened.
