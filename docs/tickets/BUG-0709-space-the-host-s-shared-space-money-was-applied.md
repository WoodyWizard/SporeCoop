---
id: BUG-0709
title: Space: the host's shared Space money was applied to a guest that plays a Space save of its own, and the guest's own earnings went to the host
status: fixed
kind: bug
stage: Space
reported: 2026-11-07
source: K2a audit of BUG-0420 (0.7.11)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state
commits: 8a54b2df
tests: space_own_save (coop_space_own_save.py); validate_tests (host_world space_other)
signature:
docs: docs/07-modes.md#modes-message-classes
related: BUG-0420, BUG-0231
---

## Report
K2a audit of the class BUG-0420 (item 5): Space was never world-gated (host_world.h: the place in Space is the star system), so in a session without rivals the host's shared Space money total was written into a guest that played a Space save of its own, and the guest's own earnings there went to the host.

## Root cause
Observed (lab 20261107-231604, `SPORE_FIX_OFF=res.space_save`): the guest in its own save (a copy of the fixture under another planet name) saw its money go from 100000 to 105000 when the host earned 5000, and the host's money rose by the guest's own 700. `guest_elsewhere()` is false in Space (NotGated), so shared.cpp never held the ledger there.

## Fix
The host names its Space save (the CRC of its save name) in WELCOME too (`welcome_world(mode, place, space_save)`); `host_world::space_other` (a connected guest in Space, the host's state in Space, both saves known and different) holds the shared money like an elsewhere guest (`res.space_save`). Limit: the host's save is the one of the guest's join; a host that loads another Space save during the session is seen after a rejoin.

## Verification
coop_space_own_save.py (fixture space; the guest leaves, loads a Space save of its own, joins again):
* FAIL with `SPORE_FIX_OFF=res.space_save`: out/lab/20261107-231604.
* PASS: out/lab/20261107-231435, out/lab/20261107-231824.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): fixed, res.space_save.
- 2026-11-08: released in 0.7.11.
