---
id: BUG-0178
title: Editors: what is still open after the 2026-10-24 report (item 4)
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-10-24
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w13-creature
commits:
tests:
signature:
docs:
related: BUG-0315, BUG-0316, BUG-0278, BUG-0283
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The Cell -> Creature switch itself has no vote (9.3 wants one): the host's click on the history's
  check starts the Creature stage at once and the guests follow; only the first creature editor waits
  for the vote.
* A guest without own creations gets its own editor on the host's creature as it was before the
  host's editor (the legless one at the start of the Creature stage). If it saves an edit, that
  edited pre-legs creature becomes its own (kept in `choices.ini`) while the host plays the legged
  one: by design (everybody evolves its own creature), but it may look like "the host's creature did
  not change". Cancel keeps the world's species.
* The guest's mating-call button needs a collected part (`S+0x69`): whether a guest touching a host's
  part token (a mirror) sets it was not checked; the test sets it by console.
* The report's Creature case ("3 rejoins did not fix it") did not reproduce as such on lane 5 with
  0.7.2.1: the guest saw the host's legged creature after the editor, also after a rejoin. Fixed
  causes that can show a stale creature: a creation sent without its records (the guest then shows
  its own body for the host), a rejoin applying the same version again (writing its package failed,
  nothing was applied).

## Root cause
Triaged per item (fix/w13-creature, 2026-10-30):
1. No vote for the Cell -> Creature switch: a feature, now BUG-0315.
2. A guest's own edit of the pre-legs creature becomes its own: by design (BUG-0278 fixed the wrong
   cases: the guest no longer keeps the host's legless creature).
3. Whether a guest touching a host's part token (a mirror) sets the mating-call button `S+0x69`:
   not checked; now BUG-0316 (the Cell fixture shows no part token, `coop.pool see 2000` on both
   games, lane 5: a check needs a kill that drops one).
4. The stale creature after 3 rejoins: not reproduced on 0.7.2.1; its known causes are fixed
   (BUG-0004). The related "guest editor shows the host's parts" (BUG-0283) turned out to be the
   test's own edit (BUG-0317).

## Fix
Nothing left here as a bug with a reproduction; the open parts went to BUG-0315 and BUG-0316.

## Verification

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-creature (wave 3): triaged, still open. Item 1 (a vote for the Cell -> Creature switch) is a feature; item 2 is by design; item 3 (whether a guest touching a mirrored part token sets the mating-call flag S+0x69) and item 4 (not reproduced on 0.7.2.1) were not checked again. Nothing here is a bug with a reproduction yet; the ticket could be split into a feature request (item 1) and a check (item 3).
- 2026-10-30: fix/w13-creature: split (item 1 -> BUG-0315, item 3 -> BUG-0316), item 2 by design, item 4 not reproduced; closed as not-reproduced.
