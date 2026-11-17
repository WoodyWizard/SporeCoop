---
id: BUG-9248
title: Creature: a partner's pack takes the target the partner hits but does not go and fight it
status: fixed
kind: bug
stage: Creature
reported: 2026-11-16
source: QA TITAN 2026-11-16
role: host
versions: 0.7.14.1-lineages63-dev
fixed_in: 0.7.14.1-lineages64-dev
branch: feat/lineages-47
commits: 85371147, 3dba5092, 764ed5b5
tests: manual Windows QA with tools/qa_win/qa.py (TITAN host 9 + guest 8, the MSI as player 2 over Tailscale; Samuth.spo)
signature:
docs: docs/re/creature.md#creature-pack-per-player, docs/re/creature.md#creature-pack-own-game
related: BUG-9129, BUG-9233, BUG-9245, BUG-9247, BUG-9249, BUG-0512, BUG-0735
---

## Report
Owner question 2026-11-16: "Does it actually attack and help impress?" TITAN QA (Samuth.spo, after BUG-9245 / BUG-9247): the partner's flock (3 summoned creatures, simulated by the game, following the partner) got the target when the partner hit a wild creature (`assists` +3, `coop.gpack` target = the victim) but stayed 1-2 m from the partner for 25 s while the victim came within 20 m and went away again; nobody ran at it.

## Root cause
Observed (TITAN QA 2026-11-16 22:00-22:08, Samuth.spo, test creatures from `coop.npc spawn 06806931` next to the host's nest):
* The game's pack order: cPosseSimulator_Relay (Steam 0xC42E40, read live) case 0x10 (the avatar's animal target) and case 0x400 (the avatar's attacker) give each pack member that is not allied with the target (0xAFEA30(member, target, 6)) and passes PosseMemberCanDefend the same call: CreatureCommandStimulus(member, 0x10, 0, 0x20030030, 0x40040000, FLT_MAX, 0x100000, 0, 0, 0, target, 0).
* The host's own flock fights: the host bit a test creature once (18 -> 14.6) and its 4 summoned members (1.2-2.5 m from it) took it to 0 within 10 s with no further bite.
* The partner's flock does not, with SetTarget (63-dev) or with that same command (creature.guest_pack_attack): 3 summoned members and 1 recruit got it (`commands=` counted, 2 declined by PosseMemberCanDefend), stayed 4-7 m away and the creature's health did not move for 35 s. The pack flag alone (+0xB58 |= 0x100) changed nothing.
* One of the partner's members added to the game's pack with cPosseSimulator_AddMember (flag 0x100 and the pack list) obeyed the next command: it went from 7.9 to 1.7 m and the creature fell 25.1 -> 14.1 in 35 s while the other members (not in the pack) stood by.
So the order works only for a creature in the game's pack (AddMember); the partner's flock is kept out of it on purpose (BUG-9233: it filled the host's pack, size +0x18, 3 at that brain level).
Not looked at: which part of AddMember the behaviour tree checks (the pack list, the posse simulator's own member entries).

## Fix
Decision 5CD (owner 2026-11-16): a pack per player, the game's own pack code run for each partner (docs/re/creature.md#creature-pack-per-player).
Built 2026-11-17 (feat/lineages-47, unreleased), `coop.fix creature.party_packs` (off: the guest pack of 63-dev):
- Phase 1, the party context: each partner has its own pack manager (the game's ctor and init) and member list; its leader (the avatar), the family herd, the host's Pack panel (hidden) and the puppet's tuning are swapped in while the game's pack code runs for it: its members' AI tick and update, its recruit (AddMember), its Summon Flock (start, update, end), its forwarded commands, the attack order. Summon and recruit put creatures into the partner's own pack (the limit of 3 recruits per pack, the summoned count); the summon ends inside the party 5 s past its time (its "done?" waits for 100 m) and the flock leaves the partner's pack as in single player. Following: the member is walked back to its partner (the game's own follow was not reproduced, see the docs). A partner's own commands to its creature are forwarded (`creature.pack_commands`, K_PACKCMD) and given to its puppet inside the party (Creature_PlayerCommand, the relay). A partner's pack never hurts or targets the host (npc.party_peace, npc.pack_spares_partners extended).
- Phase 2: the host sends each partner its pack (K_PACKLIST every 2 s); the partner's game puts those proxies into its own pack list with the game's recruit: its Pack panel, names, the first-pack hint. Its save hides that list (the proxies are never saved).
- Phase 3: a partner's recruits are noted per planet at each host save and when it leaves (worlds.ini "pack <identity>"), their pack bit off while the save is written; on a load or a rejoin the creatures of that species nearest the noted spots (10 m) join its pack again.
- Phase 4: the partner's own nest herd is the game's avatar herd inside its party, and its family's members think inside its party (`creature.party_family`).
- Around it: a summon a minute past its time ends the game's way (BUG-9245); a creature with the pack bit in no pack list loses the bit (every 5 s); the summoned count is set to what the list holds whenever no summon runs, on every game (BUG-9249).
Not achieved: the partner's pack does not bite. The members take the order and walk to the target, but no hit lands (0 hits logged), while the host's own pack, the same species and the same order (`coop.gpack hostorder`), lands every hit.
Earlier (2026-11-16): creature.guest_pack_attack gives the members the game's own attack order.

## Verification
2026-11-17 (TITAN, then the MSI as player 2 over Tailscale, Samuth.spo):
- PASS isolation: the MSI's pack (2 recruits) and the TITAN guest's (1) in their own lists, the host's list unchanged; the host recruited while both were out ("joined the pack").
- PASS Summon Flock in a partner's pack: 5 summoned into player 1's pack (summoned 5), the event ended inside the party 5 s past its time (summoned 0, members 0), the host's own summon started right after; a summon left 60 s past its time by an older run ended by itself.
- PASS the MSI's own Pack panel: its game's pack list held both members (mirror=2, its pack list 2).
- PASS save and load: 2 recruits noted at a host save, both back in player 1's pack after the host reloaded the world ("2 of its recruits back"); the pack bits back on right after the save.
- PASS follow (walked back): members within 6-18 m of the partner after a 30 m walk.
- FAIL attack: host baseline (`coop.gpack hostorder`, 2 recruits 06539202 in the host's pack): 6 hits by the two members, the creature 18 -> 0 in 9 s; the same with the MSI's pack (2-3 recruits 06539202, its hit and its forwarded order, its creature beside the target, the members 1.7-2.1 m from it): 0 hits in 24 s. The same with the members' update outside the party (`coop.gpack ctx 0`): 0 hits.
- Found and fixed on the way: a partner's recruit bit the host for 15.8 and 5.2 (the host died): now dropped and its target cleared; the host's Pack panel showed the partners' members (6 portraits): now hidden inside a party; the guest's summoned count stood at -5 (its pack refused every member): now set from the list.
2026-11-16: FAIL (63-dev SetTarget, switch off, 21:54): the partner hit a wild 067C6931 35 m away; its health stayed 71.5; the 5 members never closed in and all died to its herd within 35 s.
Not passed yet: with the order alone (switch on) the members still stood by (22:03, 22:05); one member in the game's pack fought (22:08).
FAIL (5EA switch off on both games, 07:55): the partner's attack order, 0 bites in 30 s. PASS (switch on, 07:56): the same target dead in 6 s (7 bites by the 2 recruits); TITAN partner and the MSI: 6-9 s kills, defend with no order (6 s), follow, a summoned flock.
no-fail-run: Windows QA by hand (no lab on this PC).

## Fix (decision 5EA, 2026-11-17)
The partner's own game runs its pack (`coop.fix creature.pack_own_game`, both games): the members, proxies there, are lent
to that game's AI and are its own pack, so the game's single-player follow, defend and attack order work unchanged; their
states (10 Hz) and animations go to the host, which runs no AI for them and drives them with the proxies' driving; their
bites go to the host as the partner's hits (npc::damage_filter). Code: guest_pack.cpp (own game section), npc.cpp (the
lent member's bites, health and death, animation relay), game.cpp (set_ai_skip_pack), player_rules.cpp (K_PACKSTATE 15,
K_PACKANIM 16; the attack order no longer goes to the host).

## Test (5EA)
tools/qa_win/pack_own_game.py, docs/06-testing.md#testing-pack-own-game. FAIL with the switch off on both games (0 bites
in 30 s), PASS with it on (the same target dead in 6 s), TITAN partner and the MSI: targets dead in 6-9 s, defend with no
order, follow, a summoned flock, leave. Not tested: impress help. Summon Flock: still one per world.

## History
- 2026-11-16: opened during the owner-requested pack test.
- 2026-11-16 (later): cause found (only members of the game's pack obey the pack's attack order); the order itself built (creature.guest_pack_attack); joining the pack is decision 5C.
- 2026-11-17: decision 5CD built (phases 1-4, unreleased); tested on TITAN and with the MSI; the partner's pack does not bite yet (open).
- 2026-11-17: decision 5EA built (the partner's own game runs its pack): the partner's pack bites, defends and follows; tested on TITAN and with the MSI; unreleased.
- 2026-11-17: released in 0.7.14.1-lineages64-dev.
