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
commits: 310a0311, 6fdea267
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

CORRECTION (2026-11-19, fix/w57-sync, lab run 20261119-104136): with the fix, the waiting share posted its own save in
the frame the other share went out (80 ms after that save's "game saved" box was confirmed): the game dropped it (no
box, nothing written, no save-strip lines), and only the stale-save retry 10 s later wrote it (3 saves for 2 shares).
Fix: a waiting share posts its own save kFreshGapMs (2 s) after the other share left (world.cpp).

## Fix
PendingShare keeps `asked` (share_with) and `posted` (the save's post, copied on a merge); share_wait::merge
(share_wait.h): a waiting share joins only a save posted when or after it was asked for; otherwise it waits (never two
saves at once, BUG-0112) and saves itself after the older one is written. Shares asked in one frame still share one
save (world_queue part 1). Status line "shares that waited for their own save N". Switch
`coop.fix world.share_fresh_save` (off: any save in flight).

## Verification
- Unit: npc_tests test_share_fresh PASS; npc_tests --nofix FAIL (the share asked 1 s after the save joins it).
- Lab, first night run (rel/0.7.16 c7699dca): share_fresh FAIL out/lab/20261119-104136 (rel/0.7.16 worktree): 3 saves
  for 2 shares, the dropped save above.
- Lab after the fix (fix/w57-sync 6fdea267, run dirs in the fix/w57-sync worktree): PASS share_fresh
  out/lab/20261119-111559 (the second share waits for its own save, 2 saves for 2 shares, the guest plays on). FAIL twin
  (SPORE_FIX_OFF=world.share_fresh_save) out/lab/20261119-112631 for the right reason: the second share goes out with
  the older save, 1 save for 2 shares.
- Rerun on the tree merged with rel/0.7.16 (fix/w57-sync 3801b9f5): PASS share_fresh out/lab/20261119-121505.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w56-general (WP-F1); found by the Lineages fork (PR #196), its BUG-9166.
- 2026-11-19: fixed (fix/w57-sync): see the CORRECTION under Root cause; PASS and FAIL twin run dirs above.
