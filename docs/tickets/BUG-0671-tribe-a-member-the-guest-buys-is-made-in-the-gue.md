---
id: BUG-0671
title: Tribe: a member the guest buys is made in the guest's game only and removed again
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-07
source: found in fix/w46-tribe-civ (BUG-0645 analysis)
role: guest
versions: 0.7.9
fixed_in: 0.7.10
branch: fix/w46-tribe-dance
commits: 865501d9, 8e1f4c48
tests: tribe_guest_baby, tribe_guest_baby_noeffects
signature:
docs: docs/re/tribe.md#tribe-guest-baby
related: BUG-0645
---

## Report
Lab lane 5 2026-11-07 (tribe fixture, the guest's player tribe at the host's level 2, cap 12, 8 members): the guest's game made a member at the hut (the stage's baby-born function 0xCD74A0 called on the guest, as its hut does): its tribe had it for a moment, the mod's mirror reconcile removed it (`removed` 1 -> 2), the host's game never got it (12 members before and after). docs/09-coop-design.md says new members go to whoever bought them; the host splits every new member of its own game in turns.

## Root cause
Observed: no guest -> host command for a new member (tribe.cpp has CMD_ACTION, CMD_WALK, CMD_GIVE); the host's split (host_tick, g_split_for) gives new members in turns. ? Which UI path a player uses to add a member (the mate cursor 'cursor-mate' on the hut: an order the guest relays, or the hut's own spawn) was not traced; if it is an order, the host makes the member and the turn split decides the owner.

Observed 2026-11-07 (fix/w46-tribe-dance, lab lane 5): the player's way to buy a member is a left click on the own hut:
food 10 at once, the baby 5-8 s later (lab 20261107-103440). The birth is SporeCoop::TribeStage_BabyBorn 0xCD74A0 called
from the stage's handler of message 0x63BBC42 (return 0xCD8ED3; the id is not in code: it comes from data). After the
guest's click both games ran it (lab 20261107-104338): the guest's game made a baby of its own (removed again by the mirror
reconcile) and the host's game made one only because the world effects relay (effects.other_stages) replays the guest's
hut effect in the host's game, whose data sends the message there too. With that relay off: the guest paid 10 food, the
host got no member (lab 20261107-104533, and 20261107-105153): the ticket's symptom. With it on, the member's owner was the
next in the host's turn split, not the buyer (the host's with an even member count, lab 20261107-104918).
Inferred: the earlier lab report (0xCD74A0 called directly on the guest) skipped the effect, hence no member on the host.

## Fix
`tribe.guest_baby` (mods/coop/tribe.cpp): TribeStage_BabyBorn hooked. A guest that mirrors the host's tribe makes no baby
of its own: it sends the place (MSG_TRIBE_CMD kind 3 CMD_BABY). The host takes a baby its own game makes within 4 s within
6 m (the relayed effect's) as that one, or makes it itself with the same function when none comes; the member is the
buyer's (its owner in the host's split), the guest gets the mirror. Older hosts ignore kind 3 (the relayed effect still
makes the member there). `coop.fix tribe.guest_baby off`: the guest's game makes its own baby (0.7.9).

## Verification
Test tribe_guest_baby (the guest's real mouse click on its hut; an even member count, so the turn split would give the
member to the host): FAIL with SPORE_FIX_OFF=tribe.guest_baby out/lab/20261107-104918 (the new member is the host's),
PASS out/lab/20261107-104809 (host 4 -> 5, the guest shows 5, the member is the guest's, food -10 once).
Test tribe_guest_baby_noeffects (the world effects relay off on both games): FAIL off out/lab/20261107-105153 (no member on
the host, food spent), PASS out/lab/20261107-105035-2 (the host made it itself, the guest's).
Not verified: which effect id carries the message (the relay's), a host and a guest buying within 4 s at the same hut
(the pairing could swap their owners; the count stays right), where the click charges the food.

## History
- 2026-11-07: opened.
- 2026-11-07: reproduced with the real click, fixed in fix/w46-tribe-dance (`tribe.guest_baby`).
- 2026-11-07: released in 0.7.10.
