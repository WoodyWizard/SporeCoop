---
id: BUG-9212
title: Creature: a guest cannot reach NPC creatures: they move away at the same speed or are pushed away, no attack or impress
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
related: BUG-0361
---

## Report
Owner: "Titan couldn't get close to a creature. Upon walking up to them, the other creatures would walk away at the exact same speed, or pushed away. So they couldn't attack or impress."

## Root cause
Not analysed yet (guest Titan on this PC, a new Creature world). The guest sees the host's NPCs as proxies placed by the host; on the host the guest is a puppet. Candidates: the host's NPCs keep away from the guest's puppet (their AI or the puppet's collision pushing them), and the guest's proxies follow.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not reproduced twice (Vandh.spo; the owner's Dorger.spo on TITAN: the guest walked up to a standing herd to 1 m, they did not move away, and the herd attacked the guest).

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
- 2026-11-15: not reproduced on Vandh.spo (TITAN, guest 8 walking through a standing proxy with the new `coop.abilities walk <x y z>`): the guest stops ~2 m from the proxy's middle and the proxy moves 0.32 m at most, with or without a hard hold at contact (that fix was tried and dropped, unproven). Titan did land hits later in the owner's test (02:36, proxy 70). Next: reproduce in the owner's world (a copy of the guest's received Dorger.spo is in the test host's profile as Game7), with moving herds and the host's view of the guest's puppet.
- 2026-11-15 (overnight): not reproduced in Dorger.spo (guest walked to 1 m of herd 291EE54E; no fleeing, the guest was attacked). Kept open for the next play test; docs/known-issues.md#issue-npc-keep-away.
- 2026-11-15 (owner play test, 58-dev): reproduced live by the guest BigTexas: the Lilku herd moves away when the guest comes near ("it may just be that specific creature"). No Lilku line in the host's log.
