---
id: BUG-0587
title: "Tests: tribe_guest_tribes formed no tribe with the fix off (tribes 2 -> 2): the controller's first call after a load only bakes the next species"
status: open
kind: test
stage: Tribe
reported: 2026-11-04
source: lab fix/w43-world-state 20261104-213300 and 11 more, fix/w43-realign 20261104-214542
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tribe_guest_tribes
signature:
docs: docs/06-testing.md#testing-harness-g1
related: BUG-0483, BUG-0444
---

## Report
tribe_guest_tribes failed in 12 of 13 runs of fix/w43-world-state (lab 20261104-212104 .. -214344, lanes 4/5) and in
fix/w43-realign 20261104-214542 on "with the fix off the guest's game formed a tribe with members ([])": the guest's
`coop.tribe form` with tribe.guest_tribes off printed `tribes 2 -> 2` (forms_run=1: the game's function ran) and no
tribe came in the next 8 s. It passed 3 of 3 on rel/0.7.7 48a64da8, lane 2 (lab 20261104-224512).

## Root cause
* Observed (Ghidra, GOG 2024 0xCC6440 = SporeCoop::NpcTribeController_FormTribe): before it forms, the controller
  takes the next species of its list (+0xD0, index min(tribes - 1, size - 1)) and asks IBakeManager (vt 0x1C on
  species + 0x504) whether its model is baked; when not, it asks for the bake (vt 0x3C / 0x4C) and returns without
  forming. The game calls the controller again later. With tribe.guest_tribes on, every call on the guest is blocked
  before this point, so no bake is ever asked for: the test's single call with the fix off was the first one, and it
  only started the bake.
* Observed: the failing runs had the guest's game still in the cold join's pause at the call (`busy=paused` at s
  20-50, BUG-0444) and loaded lanes; passing runs were not paused there. A first guess (the pause itself stops the
  forming) is disproved: with the pause bit held by hand (`mem.write` +0x48 = 3) the forming ran (lab
  20261104-225835: `tribes 2 -> 3`). The pause is a marker of a slow, freshly loaded game whose bake had not run.
* Inferred: the bake of the next species finishes earlier on an idle lane, so step 2 passed there.
* Also: step 1 (the fix on) is not affected by the bake (the hook returns before it).

## Fix
Test only (no game change): `coop.tribe form` now says whether the next species is baked (`(next species ...
baked|not baked yet)`) and `(blocked: tribe.guest_tribes)` when the hook blocked the call; the test calls it again,
once a second, while it was not blocked and only baked (at most 40 s), as the game's own controller does during play.
No threshold changed.

## Verification
## History
- 2026-11-04: opened.
