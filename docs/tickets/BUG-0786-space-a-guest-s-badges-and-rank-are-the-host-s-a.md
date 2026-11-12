---
id: BUG-0786
title: Space: a guest's badges (and rank) are the host's again after a rejoin
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: Discord 2026-11-09 suggestions
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-reports
commits: 0c8d2bbf, a4c922aa
tests: space_badges_rejoin
signature:
docs: docs/re/space.md#space-badges, docs/06-testing.md#testing-frame-cap
related: BUG-0678, BUG-0679, BUG-0195, BUG-0956
---

## Report
Discord suggestion 2026-11-09 "Badges should be shared with the party" (Space): the badges a guest earned in a
session are back to the host's after a rejoin.

## Root cause
Inferred, confidence medium:
* Observed (code): nothing in mods/coop handles badges (grep), no badge symbol in the .sym files; docs/re/space.md
  only mentions the badge panels and captions.
* Every player flies in a copy of the host's save and a rejoining guest loads the host's share save (as BUG-0678
  showed for the cargo hold). The badges and the rank live in the player's state saved with that world, so the
  guest's progress is replaced by the host's, and with the rank its rank rewards (tools).
* Same class as BUG-0678 / BUG-0679 (the kept cargo hold).

## Fix idea
RE first: the badge manager (object, table badge id -> level / progress, badge points, rank, their setters). Then the
space.keep_items scheme: the guest sends its badge table when it changes (a new K_* kind next to K_INV 77..79 in
space_trade.cpp), the host keeps it per player name (and the local record of BUG-0679) and gives it back when the
guest loads the host's world. Product question for the owner: personal badges (this fix) or a shared party progress
(the suggestion).

## How to test
New `coop_space_badges_rejoin.py`: the guest gets a badge (test command setting it through the game's setter), leaves
and joins again; assert the guest game's own badge field / rank after the load. Main: the host's badges: FAIL.

## Root cause (2026-11-12, fix/w48-reports)
Observed: the badges are the game's cBadgeManager (cSimulatorSpaceGame +0x70; docs/re/space.md#space-badges):
mBadgeMap (badge -> state), mStageMap, mEventCounts and the tracked card, all saved with the Space world; the rank is
derived from the earned rank badges (BadgeManager_GetRank). A rejoined guest loads the host's share save, whose
manager is the host's. Lab (fix off, lab 20261112-050858): the guest's badge B8E0B056 earned before it left was
state 2 (available) again after the rejoin, the host's state.

## Fix
`space.keep_badges` (mods/coop/space_badges.cpp, MSG_SPACE kinds 85..89, the scheme of space.keep_items): the guest
sends its tables (3 s into a Space world loaded from the host, then on every change); the host keeps the last one per
player name for its Space world; a fresh table of a player it kept one for is the host's copy, so the kept one goes
back (K_BADGES_KEPT, resent after 10 s until K_BADGES_DONE) and the guest writes it the way the game's Read does
(the maps freed, the anchor reset, operator[] per key; AddToBadgeProgress(0xC, 0) as the refresh). A Space save of
the guest's own is left alone (own_world::space_other, as BUG-0816). The rank comes back with the rank badges. No
record on the guest's disk (the hold has one, BUG-0679): a host restart or another world on the host loses the kept
badges. Product question for the owner (the Discord suggestion said "shared with the party"): this keeps each
player's own badges; a shared party progress is not done.

## Verification
`coop_space_badges_rejoin.py` (suite `space_badges_rejoin`), lane 4: the guest's game earns a badge by itself
(`coop.space badges event 2 60`: the game's Update gives B8E0B056), leaves, its game is closed and started again,
joins and loads the host's world; the earned badges (state < 2) and counter 2 must be its own again, the host's its own.
* FAIL with `SPORE_FIX_OFF=space.keep_badges`: lab 20261112-050858 (B8E0B056 missing, the host's states).
* PASS: lab 20261112-051135-2, 20261112-051312.
* A rejoin from the same running game faults at its load once a badge was earned, with the fix on and off: BUG-0956
  (the test restarts the guest's game for that reason; BADGES_RESTART=0 reproduces it).
* The game goes on with a badge chain by itself after the restore (B8E0B055 earned, B8E0B056 state 0): the test
  compares the earned set, not each state.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed (fix/w48-reports): space.keep_badges; BUG-0956 opened (a fault at a rejoin from the same game).
