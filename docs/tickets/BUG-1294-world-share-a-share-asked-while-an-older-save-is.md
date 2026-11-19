---
id: BUG-1294
title: World share: a share asked while an older save is written goes out with that older save (fork BUG-9166)
status: fixed
kind: desync
stage: All
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9166
role: host
versions: 0.7.15.2
fixed_in: unreleased
branch: fix/w56-general
commits: 310a0311
tests: unit (npc_tests test_share_fresh)
signature:
docs:
related:
switch: world.share_fresh_save
---

## Report
Found by the Lineages fork (PR #196), its BUG-9166: BUG-0112 merged a share into any save not yet written, also one
posted before the share was asked for; a new nest made 2 s after a save was posted was missing in the second player's
world. Ours had the same code: world.cpp PendingShare had no asked / posted times, tick() merged into any save in
flight. Share reasons: a join (session_glue.cpp), another mode, after the host's evolution cinematic, coop.share.

## Root cause
Inferred from the code (the same as the fork's base; the fork saw it live): the merge ignored when the save was
posted.

## Fix
PendingShare keeps `asked` (share_with) and `posted` (the save's post, copied on a merge); share_wait::merge
(share_wait.h): a waiting share joins only a save posted when or after it was asked for; otherwise it waits (never two
saves at once, BUG-0112) and saves itself after the older one is written. Shares asked in one frame still share one
save (world_queue part 1). Status line "shares that waited for their own save N". Switch
`coop.fix world.share_fresh_save` (off: any save in flight).

## Verification
- Unit: npc_tests test_share_fresh PASS; npc_tests --nofix FAIL (the share asked 1 s after the save joins it).
- Lab test share_fresh (coop_share_fresh.py: `coop.worldfix queue 1`, and again 1 s after its save is posted) written,
  NOT run yet (the lab waits for "lab ok"); SPORE_FIX_OFF=world.share_fresh_save is its FAIL twin. world_queue (one
  save for two shares in one frame) should be rerun with it.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w56-general (WP-F1); found by the Lineages fork (PR #196), its BUG-9166.
