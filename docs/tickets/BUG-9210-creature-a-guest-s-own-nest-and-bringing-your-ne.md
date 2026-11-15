---
id: BUG-9210
title: Creature: a guest's own nest (and Bringing your nest) comes minutes after it spawned: the host's new-world intro holds the nests
status: open
kind: bug
stage: Creature
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9167 BUG-9182 BUG-9208
---

## Report
Owner: "'Bringing Your Nest' only happened after the player spawned into the world."

## Root cause
Observed (host log): the world started at 02:18:43; the host's game was in the new world's intro (busy cinematic, input_lock, paused) until 02:22:39; home_nests::host_tick waits for no cinematic and no pause, so the seat nests came at 02:22:44-46, then the nest share and the guests' reload ("Bringing your nest"). The guests had spawned at 02:18:57 and 02:19:16 and played 4 minutes at the host's nest. Fix idea: guests stay behind their loading screen until their nest exists (or the nests are made before the host's intro ends).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
