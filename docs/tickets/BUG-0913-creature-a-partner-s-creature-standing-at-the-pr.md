---
id: BUG-0913
title: Creature: a partner's creature standing at the prey blocks the game's walk-and-attack (the second attacker stops 2 m short and its attack key does nothing)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: fix/w47-play-bots, lab lane 9, 2026-11-10 (play_creature's joint hunts)
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-cell
commits: f6d5a6b2
tests: partner_pass, partner_pass_ff, play_creature (the bots walk around)
signature:
docs: docs/re/creature.md#creature-partner-pass, docs/re/creature.md#creature-walk-and-use-blocked, docs/re/creature.md#creature-combat-click-target
related: BUG-0618
---

## Report
Found while settling play_creature's "joint hunts happened" (BUG-0618). The second player to attack an animal the
partner already fights, coming from the partner's side: the attack key walks its creature towards the animal and
stops it about 2 m short (gap 2.0-2.3 m, the bite reaches 1.0 m), 1.7 m from the partner's creature. Every further
press of the attack key does nothing (no step, no bite) for as long as both stand there (100 s in the probe). A
left click on the animal often picks the partner's creature instead (it stands in front): then the attack key does
nothing either (ability status 12). The player has to walk around the partner by hand.

## Root cause
Observed (lab, `creature.partner_pass` off, `coop_creature_partner_pass.py`): (a) with the host's creature 1.0-1.5 m from
an animal and on the guest's way to it, the guest's walk-and-use (attack key) stopped at gap 1.4 m behind it for 12 s
(2 of 3 such rounds, 20261112-072859); (b) a click on the animal from behind the host's creature took the host's
creature as the target (+0xE7C = its puppet + 0x5A8) in every judged round (20261112-073905, -072859, -071805).
Static (Ghidra, docs/re/creature.md#creature-partner-pass): the stop is Havok's own collision between the two
creatures (the world's hkGroupFilter, isCollisionEnabled 0x10C3F90; the locomotion has no creature avoidance), and
the click takes the hovered object, chosen by the hover update 0xB38F50 through the Creature mode's filter 0xD39070.

## Fix
coop.cpp `creature.partner_pass` (the owner's decision of 2026-11-11): in Creature, with friendly fire off and outside
Rivals (`settings::kFriendlyFire`, `spaceext::rivals() || pfind::g_test_rivals`), (a) a hook on
hkGroupFilter::isCollisionEnabled (`SporeCoop::Havok_GroupFilter_IsCollisionEnabled`) answers "no" for the pair of
our avatar and a live partner's puppet (`creature_puppet_live`; NPC proxies stay solid), and (b) a hook on the hover
filter (`SporeCoop::CreatureHoverFilter`) refuses a partner's puppet in the combat stance, so the hover and the click
take the next object along the ray. The puppets and the avatar are copied once a frame into a table the hooks read
(they may run inside Havok's step). `coop.partnerpass` prints what was skipped. Accepted (owner): players' creatures
can stand inside each other. `coop_avatar_stale_target.py` switches the fix off (it needs a click on the partner).
Switch `coop.fix creature.partner_pass off` (0.7.12).

## Verification
`coop_creature_partner_pass.py` (suite `partner_pass`, creature fixture, ~4 min; `partner_pass_ff`: friendly fire on,
the skipping off): rounds at animals not of the players' family; the host's creature 1 m from the animal; the guest
targets the animal from its other side, is put 6 m behind the host's creature and presses the attack key (must
reach the bite's reach), then clicks the animal from behind (the game's target must be the animal). Also
`coop.find rivals 1` must switch the skipping off.
* FAIL with `SPORE_FIX_OFF=creature.partner_pass`: lab 20261112-072859 (click 0 of 2, the walk stopped 1.4 m short in
  2 rounds), 20261112-073905 (click 0 of 1), 20261112-071805 (click 0 of 3).
* PASS: lab 20261112-073356, 20261112-073855 (clicks 3 of 3 / 4 of 4 on the animal, walks 2 of 2 reached).

## History
- 2026-11-10: opened.
- 2026-11-11: the owner decided: fix in 0.7.13 (walk through the partner and click through it while friendly fire is off; unchanged with friendly fire on and in Rivals), docs/plan-0.7.13.md "Owner decisions (2026-11-11)".
- 2026-11-12 (fix/w48-cell): fixed (creature.partner_pass: Havok group filter and the hover filter), FAIL and PASS runs in Verification.
