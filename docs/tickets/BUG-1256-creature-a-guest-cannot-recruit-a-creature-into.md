---
id: BUG-1256
title: Creature: a guest cannot recruit a creature into a pack (no pack recruit for the guest's social games)
status: fixed
kind: feature
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis (split from BUG-1250)
role: guest
versions: <0.7.15
fixed_in: 0.7.99
branch: rel/0.7.99
commits: f76753b8, 46dab65a
tests: guest_pack_recruit, guest_pack_recruit_off
signature:
docs: docs/known-issues.md#issue-guest-pack-recruit
related: BUG-1250, BUG-1242
switch: social.guest_recruit
---

## Report
A players' video (before 0.7.15): the host recruits a creature of its own kind into its pack; the guest tries the same
with another such creature and sings for 10 s: nothing happens. Split from BUG-1250 (whose other half, the partner's
creature under the cursor in the social stance, is fixed there).

## Root cause
A missing feature (static, Ghidra 2026-11-18, fix/w53-puppet; not run):
* The native recruit: the end of a social session (0xD9F7A0, the BT node's end) calls `cPosseSimulator_Get` 0xD53980
  and `cPosseSimulator_AddMember` 0xD542E0 on the NPC when the session succeeded (+8) and the session's flags
  `+0x1FC & 0x10` ("recruit") are set. Other callers of AddMember: 0xC20390 (the ability hit), the Summon Flock event
  0xD50EA0, 0xF1D4B0, 0xF3B480. Where the session sets bit 0x10 was not found by a byte search (no `or [r+0x1FC], 0x10`
  form); ? most likely in SocialPropose 0xDA43D0 for an NPC of the avatar's kind or of an allied species.
* A guest's social game is the host's (social.cpp: K_ENGAGE / K_ACT, the host counts the answers to the prompts of its
  NPC and applies the win as 0xD9BC40 does): `apply_success` impresses the NPC and pays its DNA, it never recruits.
  An NPC the guest's game sees as its kin is not even taken: `host_engage` refuses an NPC whose social mode with the
  HOST's avatar is 0.
* The pack is one per game: cGameNounManager +0x5C (mPosseMembers), each member follows the avatar (+0x54). In co-op
  the host's posse is the party's pack (BUG-0736: a guest's Summon Flock joins it); the guests mirror its members as
  NF_PACK allies.

## Fix
Not done (too big for the 0.7.15.1 batch). Design sketch, host-authoritative:
1. Recruit decision on the host: the guest's K_ENGAGE of an NPC that the native game would offer a recruit session
   (to be found in SocialPropose 0xDA43D0: kin of the guest's species, or a species allied with the party) starts a
   recruit game (its prompts as now); the win calls `cPosseSimulator_AddMember` on the HOST's NPC (the pack bit
   +0xB58 0x100, mPosseMembers) and records the owner (the guest's player id) in the host's NPC record.
2. Who the member follows: members owned by a guest tick their AI with that guest's puppet as mpAvatar (the
   cGameNounManager +0x54 swap around NPCTickAI the mod already does for NPCs that target a partner, `npc.cpp
   ai_target`, game.cpp `h_NPCTickAI`), so they follow, defend (BUG-0735 path) and fight for the guest's creature.
3. Limits: the native pack size by the brain level, counted per player (the host's own members by the native code,
   a guest's by the host for that guest's brain level from its PlayerState).
4. The guest's view: its proxies of its own members get NF_PACK plus the owner id; its UI (pack cards 0xE24C70) needs
   them in its own posse vector: add the proxies there without running their AI (proxies have none), remove them when
   the proxy goes.
5. Lifetime: a guest that leaves: its members go back to the host's pack or are released (the native release
   +0x135 = 0, +0xF90 = 0 as SummonEvent_Update does); members are the host world's creatures and are saved with it
   (only the owner record is the mod's, never saved).
6. Tests: a guest's recruit of a kin by real input (the click and the key), the member follows the guest's creature on
   both games, the brain limit, the leave.

## Verification
PASS: out/lab/20261124-155436 (guest_pack_recruit, 0 faults: guest recruits, enforces brain limit, dismisses, cleans up on leave).
FAIL: out/lab/20261124-161419 (guest_pack_recruit_off: my_pack=0, local_count=0).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-puppet): split from BUG-1250; static analysis of the native recruit and a design sketch; left open.
- 2026-11-18: planned for 0.8 (the owner's 0.7.15 decision: the guest's own pack is 0.8 work).
- 2026-11-24: released in 0.7.99.
