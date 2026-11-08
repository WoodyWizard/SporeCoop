---
id: BUG-0735
title: Creature: the host's pack never defends a guest's creature (TakeDamage relays 0x400 to the pack only for the avatar)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0418 sibling list, K2b audit 2026-11-08
role: host
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-b
commits: 8911e865, e896b4a8
tests: pack_defends_partner, pack_defends_partner_slow (coop_party_defence.py)
signature:
docs: docs/re/creature.md#creature-player-species-callers
related: BUG-0418, BUG-0374, BUG-0556
---

## Report
K2b audit of the class BUG-0418: nobody defended a guest: the host's pack (and the creatures a guest recruited, who are host pack members) stood by while an animal attacked the guest's creature (npc.cpp ai_target skips the pack; the game's defence runs for the avatar only). The avatar-only post 0xD335362C of CreatureAttackTarget is not pack defence (its listeners: a tracking event and the part reward roll, in the attacker's own game).

## Root cause
Static (Ghidra, GOG 2024): TakeDamage 0xC075E0 on the avatar posts 0x400 and calls cPosseSimulator_Relay 0xD52E40(0x400, 0): each member that PosseMemberCanDefend 0xD7D0E0 gets CreatureCommandStimulus 0xC0C070(0x10, ..., sender = the attacker). The puppet is no avatar. Observed (lab 20261108-040752, `SPORE_FIX_OFF=npc.pack_defends_partner`): five pack members within 25 m of the guest, an animal hit the guest's puppet, no member took it as its target.

## Fix
creature_hp::pack_defend, called where the host forwards an NPC's hit on a puppet (npc.cpp damage filter): the game's own call for each pack member within 80 m that PosseMemberCanDefend (rate-limited to once per 1.5 s per attacker). `coop.fix npc.pack_defends_partner off`: nobody defends the guest (0.7.10).

## Verification
FAIL: out/lab/20261108-040752. PASS: out/lab/20261108-035853 (all 5 members target the attacker's combatant +0x5A8 within 8 s), out/lab/20261108-041412 (the _slow twin at 20 fps over the player network).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `npc.pack_defends_partner`; FAIL and PASS runs above.
