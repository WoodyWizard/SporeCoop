---
id: BUG-0544
title: Space diplomacy: a guest's changes while the host is out of Space are lost on both copies
status: fixed
kind: desync
stage: Space
reported: 2026-11-04
source: BUG-0194 (open end of space_diplomacy.cpp)
role: both
versions: 0.7.5 and earlier
fixed_in: 0.7.6
branch: fix/w42-space-play
commits: f8e809cb
tests: space_diplo_away
signature:
docs: docs/re/space.md#space-play-w42
related: BUG-0194
---

## Report
Found reading space_diplomacy.cpp for BUG-0194 ("what may drift"): a gift, a mission, a war or an alliance a guest
makes with an NPC empire while the host is in an editor (or an adventure) entered from Space is lost: the host
never gets it and its next periodic table takes it away from the guest too, although the gift's money was paid.

## Root cause
Inferred from the code, then seen in the test (FAIL run below): the inbox task dropped every partner message while
`in_space()` was false. A guest asks for the host's table when it comes back, so only the host loses them; the
guest's 8 s guard against stale tables runs out long before the host's next table.

## Fix
The messages wait in the Space world they arrived in (`spaceext::world_epoch`, at most 512) and are replayed when
the game is back in Space; the host sends no periodic table while such messages wait. Switch
`coop.fix diplomacy.keep_away off`.

## Verification
- FAIL (diplomacy.keep_away off): out/lab/20261104-114312 (the host never got the gift, the guest lost it
  with the host's next table).
- PASS: out/lab/20261104-114153; space_diplomacy on the same build: out/lab/20261104-114437.

## History
- 2026-11-04: opened and fixed on fix/w42-space-play.
- 2026-11-04: released in 0.7.6.
