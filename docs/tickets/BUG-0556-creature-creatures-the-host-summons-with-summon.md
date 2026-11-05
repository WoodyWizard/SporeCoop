---
id: BUG-0556
title: Creature: creatures the host summons with Summon Flock are not friendly (green) to the guest
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: both
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-guest-player
commits: a8845587, ca09f017
tests: coop_creature_flock.py
signature:
docs: docs/re/creature.md#creature-player-rules
related:
---

## Report
Both players: the flock the host calls is not allied with the guest; the guest does not see them as friendly.
Reports: SporeCoop-report-2026-11-04 180352, 180419 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s2129 / s2144: the host's screen shows its flock with hearts (allied); at s2144 the guest's world is black (only the HUD) while the host gets "<guest> is responding again" (a guest hitch).
Not analysed yet.

## Root cause
* Observed (Ghidra + lane 9): Summon Flock (ability 0x33 -> drama event kind 8 cSummonCreatureEvent, start 0xD50EA0)
  makes a herd (+0x192 = 1) and puts each creature into the host's pack (cPosseSimulator_AddMember 0xD542E0: +0xB58 |=
  0x100); its update 0xD512A0 keeps the hearts effect 0x6A17B05A on them. The game's friendly predicate (vt 0xD4
  `SporeCoop::CreatureFriendly` 0xC0C3B0: relation 6, impressed, own species) does not read the pack bit; the guest's
  proxies of the flock had neither the pack bit nor the hearts: strangers. `coop.drama start 8` on the host: 4-5
  creatures of 0672579F in the pack, hearts on the host's screen, none on the guest's (screenshots, lane 9).

## Fix
The host sends the pack bit and the summoned herd in the NPC state (NF_PACK 8, NF_SUMMONED 16). Guest: vt 0xD4 answers
true for proxies of the host's pack (`creature.party_allies`, player_rules.cpp); proxies of a summoned flock get the
hearts effect 0x6A17B05A from the game's own pool (`npc.flock_hearts`, npc.cpp).
`coop.fix creature.party_allies off` / `npc.flock_hearts off`: the old behaviour.

## Verification
coop_creature_flock.py (suite creature_flock): the host's game starts its Summon Flock event (`coop.drama start 8`),
the guest's game must answer vt 0xD4 true and hold the hearts effect for every proxy of the flock.
* FAIL with `SPORE_FIX_OFF=creature.party_allies,npc.flock_hearts`: out/lab/20261104-212613 (4 of 4 not friendly, no hearts).
* PASS: out/lab/20261104-210851, out/lab/20261104-220621.
* Not verified: the guest's own Summon Flock (a flock its own game makes); the cursor's pack kind 10 (the guest's
  proxies do not get the bit 0x100 itself, on purpose: AddPosseMember side effects).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), not reproduced.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): fixed: the host's pack / flock is friendly on the guest and shows its hearts.
- 2026-11-05: released in 0.7.7.
