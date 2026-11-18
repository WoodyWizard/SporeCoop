---
id: BUG-9129
title: Creature: the guest has no pack: Summon Flock does nothing and the host's creatures cannot join the guest's party
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: owner reports on 0.7.8-lineages49-dev ("Summon Flock isn't working for the connected player still"; "connected player is unable to create a party with the host's creatures")
role: guest
versions: all
fixed_in: 0.7.8-lineages51-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8: guest `coop.prules roar 33` (its Summon Flock node tick), guest `call SporeCoop::cPosseSimulator_AddMember` on a proxy, host `coop.gpack`, `coop.gpack recruit <guest> <npc>`, `coop.gpack summon <guest>`
signature: guest log "drama: summon event starts (guest)", the guest's status "Pack: alone", nothing on the host
docs: docs/re/creature.md#creature-roar-relay
related: BUG-0556 BUG-0501 BUG-0418
switch: creature.guest_pack
---

## Findings
* Summon Flock is the player's consequence ability 0x33: BT_SpecialAbilityTick 0xD8E840 -> DramaManager_StartKind(8)
  -> cSummonCreatureEvent (start 0xD50EA0), which makes a herd and adds each creature to the avatar's pack
  (cPosseSimulator_AddMember 0xD542E0). On a guest the event starts in its own game (log above) and no flock is
  left: the host owns every NPC (npc.cpp removes the guest's own), and the host never hears of it.
* Recruiting (befriending a creature into the party) is the same missing piece: the pack is the avatar's posse in
  each game; the host's game has one posse (the host's), the guest's game has no NPCs of its own to keep.
* A fix needs the host to run a second pack that follows the guest's puppet: the summon / recruit as a request to
  the host (as the roar relay, BUG-0501), the members added there and steered after the puppet, sent to the guest
  as NF_PACK proxies (BUG-0556 makes them allies). Not started.

## Fix (guest_pack.cpp, `creature.guest_pack`)
* The host keeps a pack per guest. Summon: the guest's first tick of its 0x33 node sends MSG_PLAYER_RULE kind 3; its
  own drama event 8 is refused (drama.cpp). The host runs DramaManager_StartKind(8); the new entries of its pack list
  (cGameNounManager +0x5C) are taken out of the host's pack (bit 0x100 and the list entry) and placed beside the
  guest's puppet. Recruit: the guest's cPosseSimulator_AddMember on a proxy is hooked: kind 4 {npc id} to the host,
  the proxy stays out of the guest's pack (and its saves).
* Members move into the herd the mod keeps for the guest's puppet (Creature_SetHerd 0xC04700; its home follows the
  puppet): in their own nest herd they followed 8 m and then walked back home to 50 m. A leash every 0.5 s
  (MoveTo when more than 6 m away, back beside the puppet beyond 60 m).
* Fights: a guest's hit the host applies (MSG_DAMAGE_REQ) sets the members' target to that NPC; an NPC's hit on the
  puppet sets it to the attacker (SetTarget vt 0x84). Members are the party (creature_hp::host_party): no hits on
  partners, not enemy-scaled, never a partner's target; the guest gets them as NF_PACK (friendly).

## Verification (QA host 9 + guest 8)
* Guest `coop.prules roar 33` (its own Summon Flock, ability 14): summon_sent=1, the guest's drama summon refused=1,
  the host "the flock summoned 3 for player 1", all 3 beside the guest (1-3 m).
* Guest AddMember on proxy #6: recruit_sent=1, the host's guest pack 4 members, #6 3.0 m from the guest, friendly=1 on
  the guest.
* Follow: the guest moved ~30 m: the flock 28 -> 14 -> 2 m. Without the herd move a recruit walked back to 50 m.
* Fight: the guest's hit on #10 set the recruit's target (assists=1); NPCs that hit the guest drew it (defends=12).
  A recruit of a non-allied species kept aiming at the guest (target cleared by npc.pack_spares_partners) and died:
  only allied species can be recruited in the game.
* 0 faults on both games. A crash of the guest's game came from the test itself (AddMember called with a null
  creature through `call`); the hook now refuses a null.
* Also: player_rules' test command is `coop.prules` (coop.cpp's campaign `coop.rules` took the name; the lab tests
  coop_creature_roar / stun / flock / nest_ally used the shadowed one).
