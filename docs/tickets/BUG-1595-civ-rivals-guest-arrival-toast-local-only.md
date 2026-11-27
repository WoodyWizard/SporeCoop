---
id: BUG-1595
title: Civ Rivals: guest arrival specialty hint broadcasts to all peers instead of local toast
status: fixed
kind: bug
stage: Civ
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: guest
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 20fa22f2
tests: rivals_civ_spec_sw, make test
signature: guest Civ Rivals arrival hint echoed as broadcast notice to other players
switch: none (local C.feed vs broadcast C.notice call in civ_rivals.cpp)
docs:
related:
---

## Report
The guest's local Civ Rivals starting-specialty arrival toast was routed through `C.notice`, broadcasting it to other peers.

## Root cause
`civ_rivals.cpp` called `C.notice` instead of `C.feed` for the local guest arrival hint.

## Fix
Switched the Civ Rivals guest arrival specialty hint in `mods/coop/civ_rivals.cpp` to local-only `C.feed`.

## Verification
- PASS run: `out/lab/20261127-001053` (`rivals_civ_spec_sw`, PASS) and `make test`.
- no-fail-run: single-line local-feed routing fix verified in `rivals_civ_spec_sw`.
