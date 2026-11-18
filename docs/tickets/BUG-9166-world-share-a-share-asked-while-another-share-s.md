---
id: BUG-9166
title: World share: a share asked while another share's save is being written goes out with that older save (a new nest missing)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: qa
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + 2 TITAN guests + the MSI as a guest over Tailscale (relayed), 2026-11-13, all players with a creature of their own)
signature:
docs: docs/re/creature.md#creature-migration-own-nests
related: BUG-0112 BUG-9135 BUG-9167
switch: world.share_fresh_save
---

## Report
QA: after the party claimed a new nest the host made the three players' nests 2 s apart; the second player's world had no nest of its own ("nest: the host's home nest (235 -439 -88) has no nest herd here", every 10 s).

## Root cause
Observed in the host log: "world: saving for the share of player 3" at 02:12:55.57, seat 2's nest made at 02:12:57.58, "world: the share for player 1 goes out with the save for player 3" at 02:12:57.60: BUG-0112 merges a share into any save not written yet, also one posted before the share was asked for (before its reason existed).

## Fix
world.share_fresh_save (on). PendingShare keeps when it was asked for and when its save was posted; a share merges only with a save posted after it was asked for; while an older save is being written it waits (one save at a time, BUG-0112) and then gets a save of its own ("waits for a save of its own ... BUG-9166").

## Verification
no-fail-run: manual on TITAN + the MSI (no Windows lab lane). FAIL (the code before): above. PASS: `coop.homes extinct 1` and `extinct 2` back to back: "the share for player 1 waits for a save of its own", then its own save; the guest joined its new nest ("home nest at (150.0 -482.3 -24.1)"), twice (02:23, 02:29).

## History
- 2026-11-13: opened.
