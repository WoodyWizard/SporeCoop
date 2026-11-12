---
id: BUG-0818
title: Space: the wsync 'stages' row writes the host's stage consequence traits into a guest's own Space save
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-own-world
commits: dda0afe5, 946f4dd2
tests: own_space
signature:
docs: docs/re/space.md#space-own-save-hold, docs/re/host-centric.md#host-centric-own-world, docs/06-testing.md#testing-own-world
related: BUG-0577
---

## Report
A guest playing its own Space save while connected gets the host's stage history (the History bar, the archetype's trait cards and abilities), and its save keeps them. Found by reading the code.

## Root cause
Observed at runtime: with the guest in a Space save of its own, the host's stage 0 trait (cPlayer +0x10FC) changed to A5656E84 and the guest's own save took it within 10 s (lab 20261112-030215). MSG_WSTATE is not dropped in Space and the stages row is active there. The visible effect on the History bar and the archetype cards is inferred, not looked at.

## Fix idea
Do not apply the stages row in Space while the guest plays another save (host_world::space_other) or while that is unknown.

## How to test
Two Space saves with different stage traits; the guest loads its own one, then joins: compare `coop.wsync dump stages` on the guest before and after: FAIL = the host's traits; PASS = its own.

## Fix
wsync's guest tick applies no row in Space while own_world::space_other() (`coop.wsync` space_own_held). Switch `coop.fix wsync.space_own off`.

## Verification
Test `own_space` step 4 (`coop.wsync stage 0 <hex>` on the host, `coop.wsync dump` stages on the guest).
* FAIL: out/lab/20261112-030215 (`SPORE_FIX_OFF=wsync.space_own`, lane 5): 00000000 -> A5656E84.
* PASS: out/lab/20261112-025609, out/lab/20261112-031100: 00000000 kept.

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12 (fix/w48-own-world, O1 of 0.7.13): fixed with wsync.space_own; FAIL 20261112-030215, PASS 20261112-025609 / 031100.
