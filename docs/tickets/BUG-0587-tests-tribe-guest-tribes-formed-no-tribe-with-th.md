---
id: BUG-0587
title: "Tests: tribe_guest_tribes formed no tribe with the fix off (tribes 2 -> 2) on loaded lanes, and its twin run passed step 1 under W2"
status: fixed
kind: test
stage: Tribe
reported: 2026-11-04
source: lab fix/w43-world-state 20261104-213300 and 11 more, fix/w43-realign 20261104-214542
role: guest
versions: 
fixed_in: 0.7.7
branch: fix/w43-tests
commits: 74fef091 d26982ff e5680e8e 840eb95a 517e05c2
tests: tribe_guest_tribes
signature:
docs: docs/06-testing.md#testing-harness-g1
related: BUG-0483, BUG-0444
switch: none
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
* Not confirmed: whether the failing lanes' species was unbaked. On lane 2 the next species was `baked` in every run
  (lab 20261104-230733, -231514); the reply now also prints the tribe nouns and the spawn markers the controller needs
  (4 markers on the fixture, one used per tribe), so the next such failure says which condition stopped it.
* Observed (second harness defect): since W2 (realign.villages, BUG-0564) the twin run `SPORE_FIX_OFF=
  tribe.guest_tribes` passed step 1 although the guest's controller formed 3 tribes: W2 removed the villages within
  4 s (lab 20261104-231126). The test no longer caught BUG-0483.
* Also: step 1 (the fix on) is not affected by the bake (the hook returns before it).

## Fix
Test only (no game change): `coop.tribe form` now says whether the next species is baked (`(next species ...
baked|not baked yet)`) and `(blocked: tribe.guest_tribes)` when the hook blocked the call; the test calls it again,
once a second, while it was not blocked and only baked (at most 40 s), as the game's own controller does during play.
realign.villages is off from the start until step 3 (it was off for step 2 only). No threshold changed.

## Verification
* The twin: wrong run out/lab/20261104-231126 (SPORE_FIX_OFF=tribe.guest_tribes, step 1 ok on the bug); right run
  out/lab/20261104-231338 (step 1 FAIL with 3 guest-only tribes, step 3 FAIL, as BUG-0483's FAIL 20261103-234937).
* PASS: out/lab/20261104-231514 (3 of 3; `baked`, 2 nouns, 4 markers, `tribes 2 -> 3` with the fix off).
* The 12-of-13 failure of fix/w43-world-state's lanes was not reproduced on lane 2 (no FAIL run of that case).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-tests): the bake found in 0xCC6440, the W2 masking of the twin fixed; left open (cause on lanes 4/5 not confirmed).
- 2026-11-05 (fix/w44-tests, G2): kind: **test** (the test's single call of the controller met the game's own
  bake-before-forming step; G1's retry, 74fef091, in 0.7.7). Since that change no run failed: G1's 3 + 3 (lab
  20261104-230733, -231514), fix/w44-tribe-guest 20261105-115848, rel/0.7.7 20261105-015320, and here 3 of 3 on lane 9
  while 4 other agents ran their lanes (out/lab/20261105-134354: `tribes 2 -> 2 ... (blocked: tribe.guest_tribes)`,
  fix off `tribes 2 -> 3`, `baked`, 2 nouns, 4 markers each). Not confirmed: which condition stopped the forming on
  fix/w43-world-state's lanes 4/5 (those runs had the single call; their branch is in 0.7.7, which passes). Closed as
  fixed by the retry; reopen with the reply line (`next species ... baked|not baked yet`, nouns, markers) if it comes back.
