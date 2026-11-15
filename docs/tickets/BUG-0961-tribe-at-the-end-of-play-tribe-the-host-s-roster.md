---
id: BUG-0961
title: Tribe: at the end of play_tribe the host's roster has 2 cards (the knocked-out chief dark) and the guest's 3 (the chief's card red)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-tribe-mirrors, play_tribe key frame 03_end, lab 20261112-102606
role: guest
versions: 0.7.13 (unreleased)
fixed_in: 0.7.13
branch: fix/w48-rest
commits: fcaec2b8
tests: tribe_roster_cards, play_tribe
signature:
docs: docs/re/tribe.md#tribe-roster-cards
related: BUG-0935, BUG-0173, BUG-0620
---

## Report
Key frames of play_tribe in lab 20261112-102606 (fix/w48-tribe-mirrors, with BUG-0935's hunger fix): at 02_mid both
rosters agree (3 pale cards with green bars). At 03_end (`out/lab/20261112-102606/frames/play_tribe/03_end_host.jpg`,
`03_end_guest.jpg`, crop the right edge): the host's roster has 2 cards, the chief's dark (knocked out) and one member;
the guest's has 3, the chief's card tinted red. The test's own check ("the same members, owners and chiefs on both",
from `coop.tribe` rows) passed. Not seen in tribe_mirror_hunger (no knockouts there).

## Root cause
Proven (2026-11-12, fix/w48-rest; Ghidra and lab lanes 4 / 5, docs/re/tribe.md#tribe-roster-cards): the roster is
the stage game UI's map citizen -> card ([cTribeUI_Get]+0x240, +0x98, size +0xA8); the host's game takes a dead
member's card off through the stage's message listener (0xCD81F0 -> 0xCD51F0 -> 0xCCBCE0). The guest's mirror of a
member that died on the host is taken out of the guest's tribe by the mod (detach_body, BUG-0620): the tribe's list
shrank, the card stayed. Lab (members killed with `coop.tribe pincer`): host 3 cards / 3 members, guest 5 cards / 3
members, 5 s and 20 s after. The chief's red or dark card is its health (knocked out on the host, 0.3 when others
stand): not separately reproduced here.
Was (inferred): Inferred candidates: a knocked-out member is shown dark by the host's game (its dead flag) and the
guest's mirror played the death (tribe.ko, BUG-0173) which its roster shows red; the third card on the guest a member
that is a body on the host (out of its tribe's list, BUG-0620) while the guest's copy is still listed.

## Fix
mods/coop/tribe.cpp `body_card_off` (detach_body): when a mirror is taken out of its tribe as a body, the guest's game
removes its roster card with the stage's own removal (SporeCoop::TribeStage_RemoveMemberCard 0xCD51F0). Switch:
`coop.fix tribe.body_card off`.

## Verification
Test `tribe_roster_cards` (tools/tests/coop_tribe_roster_cards.py, tribe fixture): one host and one guest member killed
on the host; the roster's cards (the game UI's map size) equal the members on each game and on both, 5 s and 20 s
after; key frame `roster`.
FAIL (SPORE_FIX_OFF=tribe.body_card): out/lab/20261112-142412 (guest 5 cards, 3 members; host 3 / 3).
PASS: out/lab/20261112-142414, out/lab/20261112-142541.

## History
- 2026-11-12: opened.
- 2026-11-12: reproduced (a dead member kept its card on the guest), the card taken off with the body (tribe.body_card), fix/w48-rest.
- 2026-11-12: released in 0.7.13.
