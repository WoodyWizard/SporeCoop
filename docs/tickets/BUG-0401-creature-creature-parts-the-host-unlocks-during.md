---
id: BUG-0401
title: Creature: creature parts the host unlocks during a session never reach the guests' games (their own editor's palette)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: lab monitor (feat/w32-monitor-diag, unlocks rule plant), lane 6
role: guest
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w33-unlocks
commits: 4cc57da 145a981
tests: parts_shared (tools/tests/coop_parts_shared.py), monitor_plant (unlocks rule, with parts.shared off on the host)
signature:
docs: docs/re/creations.md#creations-unlock-part, docs/re/creations.md#creations-part-pickup, docs/06-testing.md#testing-parts-shared
related: BUG-0368
switch: parts.shared
---

## Report
monitor_plant (out/lab/20261101-130546-feat-w32-monitor-diag, lane 6, creature fixture): the host unlocks 3
creature parts in its own game (`coop.parts unlock 3`, cCollectableItems::UnlockPart as a found part does):
host `parts=57:...`, the guest stays at `parts=54:C81D84FB` for the rest of the session (90+ s); the
monitor's unlocks rule fires. Since BUG-0368 the guest's own editor offers its cPlayer's collected parts
(cPlayer +0x10E8), which come from the host's save at the share and are never updated.

## Root cause
Observed: nothing in the mod sends a part unlock (no UnlockPart hook or message); the guest's collected
parts are those of the save it loaded. Ghidra + lane 6: a part found in play (a skeleton's reward
0x5371F11, a creature's drop 0xD335362C, the rewards of 0x49B56D7) is unlocked by the creature stage's
event handler 0xD47FC0 -> 0xD47E50 -> 0x5976D0 -> 0x597410 in that player's game only, without calling
UnlockPart (docs/re/creations.md#creations-part-pickup). So the host's finds never reached the guests,
and a guest's finds were lost at its next load of the host's world.

Intended rule: shared progress (docs/07-modes.md 7.1 "Shared progress": DNA, food, money, computed by
the host; "The host decides", 7.6): the party has one set of collected parts, the union, held by the
host's game; a part any player finds is unlocked for all; nothing is taken away.

## Fix
`parts.shared` (parts.cpp, MSG_PARTS 53): every game polls its collected parts (the parts= census, 1 Hz
in Creature: a hook on UnlockPart would miss the finds); a guest that plays the host's world sends the
parts the host's set lacks (resent every 3 s, at most 5 times a key), the host unlocks them
(UnlockPart, cost 0) and sends its whole set (at once on a change, else every 10 s) and the news with
the finder ("Bob found a new creature part: it is in your editor too"); a guest unlocks what it lacks
of the host's set of the same world, also after a reload. Nothing is ever locked. The host's save at a
join holds the union. Older SporeCoop ignores MSG_PARTS. Test helper `coop.parts find <level>` (the
game's own "a part was found").

## Verification
FAIL: SPORE_FIX_OFF=parts.shared parts_shared, lab 20261101-141549 (lane 6: the host's find 54 -> 55
stays off the guest for 21 s, the guest's find off the host).
PASS: parts_shared, lab 20261101-141341 (lane 6: host find level 1 on the guest in 1 s, guest find on
the host in 1 s, the guest's own editor 56 of 312 available and 256 locked, after a rejoin 56 = 56).

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w33-unlocks: the party shares its collected parts (parts.shared); fixed (unreleased).
- 2026-11-02: released in 0.7.3.4.
- 2026-11-02 (0.7.3.4, guest's mark): after a crash and a reload of the host's older save the guest kept parts it had unlocked later ('parts I didn't unlock'): parts are a union that is never taken away; by design for now, revisit with save-bound parts.
