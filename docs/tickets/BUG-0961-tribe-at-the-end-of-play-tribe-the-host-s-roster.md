---
id: BUG-0961
title: Tribe: at the end of play_tribe the host's roster has 2 cards (the knocked-out chief dark) and the guest's 3 (the chief's card red)
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-tribe-mirrors, play_tribe key frame 03_end, lab 20261112-102606
role: guest
versions: 0.7.13 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0935, BUG-0173, BUG-0620
---

## Report
Key frames of play_tribe in lab 20261112-102606 (fix/w48-tribe-mirrors, with BUG-0935's hunger fix): at 02_mid both
rosters agree (3 pale cards with green bars). At 03_end (`out/lab/20261112-102606/frames/play_tribe/03_end_host.jpg`,
`03_end_guest.jpg`, crop the right edge): the host's roster has 2 cards, the chief's dark (knocked out) and one member;
the guest's has 3, the chief's card tinted red. The test's own check ("the same members, owners and chiefs on both",
from `coop.tribe` rows) passed. Not seen in tribe_mirror_hunger (no knockouts there).

## Root cause
Not analysed. Inferred candidates: a knocked-out member is shown dark by the host's game (its dead flag) and the
guest's mirror played the death (tribe.ko, BUG-0173) which its roster shows red; the third card on the guest a member
that is a body on the host (out of its tribe's list, BUG-0620) while the guest's copy is still listed.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
