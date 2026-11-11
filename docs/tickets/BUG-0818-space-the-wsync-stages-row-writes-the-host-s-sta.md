---
id: BUG-0818
title: Space: the wsync 'stages' row writes the host's stage consequence traits into a guest's own Space save
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0577
---

## Report
A guest playing its own Space save while connected gets the host's stage history (the History bar, the archetype's trait cards and abilities), and its save keeps them. Found by reading the code.

## Root cause
Observed in the code for the write, confidence high; the visible effect on the archetype is inferred. wsync.cpp:1341 row {3, "stages", ..., planet, ...}: planet() (:153-156) includes kSpace, and s_stages (:258-273) writes cPlayer +0x10FC..+0x110C. MSG_WSTATE is not held when the guest plays another world in Space. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Do not apply the stages row in Space while the guest plays another save (host_world::space_other) or while that is unknown.

## How to test
Two Space saves with different stage traits; the guest loads its own one, then joins: compare `coop.wsync dump stages` on the guest before and after: FAIL = the host's traits; PASS = its own.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
