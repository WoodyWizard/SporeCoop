---
id: BUG-9224
title: Cell: the knocked-out ring timer does not go down
status: fixed
kind: bug
stage: cell
reported: 2026-11-15
source: owner play test 2026-11-15
role: all
versions: 0.7.14.1-lineages58-dev
fixed_in: 0.7.14.1-lineages59-dev
branch: feat/lineages-47
commits: 95378ac3
tests: none yet (built; the owner's session was running)
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "The timer was not going down on Cell."

## Root cause
Observed in the code: cellsync::own_left returned the seconds left (60) where ring_timer::draw takes the fraction left (1 .. 0, as creature_rescue::own_left): the ring stayed full until the last second.

## Fix
- own_left returns ring_timer::fraction(left, down_time()). Switch cell.ring_fraction.

## Verification
no-fail-run: built only; to be checked on TITAN + the MSI after the owner's play session

## History
- 2026-11-15: opened.
