---
id: BUG-9308
title: Creature (guest): a creature a partner wins in a social game never joins the partner's pack
status: fixed
kind: bug
stage: Creature
reported: 2026-11-20
source: decision 7L (owner, 2026-11-20) and the 67-dev guest report 20261120-205700
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-impress
commits: 0e2a2801, a5a8cf31, 9d64f85d, 59d47c0a
tests: tools/qa_win (live two-game QA on the MSI and TITAN, 2026-11-21, switch off and on)
signature:
docs: docs/re/creature.md#creature-native-recruit, docs/re/creature.md#creature-partner-social-goal
related: BUG-9297, BUG-9305, BUG-9129, BUG-9248
---

## Report
Decision 7L (owner, 2026-11-20): "If a partner impresses a creature to join a pack, it should go to their pack like
native spore." The 67-dev guest report: the guest "couldn't add creatures to their pack. Tested on Host's species, as
well as their own."

## Root cause
Observed in the code: a partner's social game runs on the host (social.cpp host_act / apply_success); its win only
set the impressed flag, applied the species event and paid DNA. Nobody called cPosseSimulator_AddMember. The partner's
recruit path (player_rules h_AddMember -> K_RECRUIT -> guest_pack::recruit) needs an AddMember call in the partner's
own game, which runs no social session for a mirror (BUG-9129 tested it by calling AddMember by hand). Also the host
refused any game with an impressed creature, and the guest opened no meter for one.
Native (the .sym note on AddMember and the public decompilation, docs/re/creature.md#creature-native-recruit): the
success of a game with a creature whose social goal is 4 sets the session's recruit flag and gives no DNA; the
session end adds it to the avatar's pack. Inferred: goal 4 = a friendly creature that can join (allied species, own
species, impressed); not measured what an impressed creature or a pack member returns.

## Fix
Done (fix/l47-impress, switch `social.partner_recruit`, `coop.fix social.partner_recruit off` = 67-dev): the host
computes the creature's social goal for the partner inside guest_pack's party context (its puppet as the avatar, its
pack as the posse). Goal 4: the game runs also on an impressed creature (host_engage, host_act, the prompt tick,
apply_success), its win pays no DNA, and the creature goes into that partner's pack through guest_pack::recruit (the
game's own AddMember in the party context; the partner's game gets it as its own pack member by the 5CD pack list).
Host log "social: player P won a recruit game (social goal 4) with npc N: it joins that player's pack" (or "the pack
refused it"); `coop.social` (host) `recruits=` / `recruits_refused=`. The guest opens its meter for a goal-4 creature
also when it is impressed and posts no part-reward message for it (`coop.social meter` `mode=4`).

2026-11-21 (fix/l47-qa, live QA): CORRECTION: goal 4 is an allied species only (relation 6, the creature in no pack,
the pack not full); an impressed creature of a neutral species gets goal 0 (CreatureSocialMode read live,
docs/re/creature.md#creature-partner-social-goal). And inside the party context GetRelationship returned 6 for every NPC,
so every partner win was a recruit game (no DNA, straight into the pack). Fix `social.partner_relation` (switch, off =
the first QA's behaviour): the relation is read outside the party context (the party shares the host's table); only a
relation-6 NPC asks the party context for the goal; the host's engage / act checks use the partner's goal. Tool
`coop.social pmode <npc> <player>` (host): the goal and relation for the host and inside that player's party.

## Verification
no-fail-run: pending the lead's two-player QA (built only).
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): not verified: clicks of the guest in social stance on the host's own pack members (npc 21, `family`, mode=4) never started a social game (engaged stayed -1), so the recruit path never ran.

- Live QA 2026-11-21, second run (QA agent, fix/l47-qa: aeaedfc3 + `social.partner_relation`, MSI two games, Creature
  Vandh; host MSITribeHost instance 2, guest MSIGuest instance 1):
  * Why the first test's clicks never engaged: the guest's clicks on its own species' nestmates are the native mate
    click (cursor kind 9: the guest's game opened its creature editor, seen); the host's family / pack members are
    relation 6 but a pack member (+0xB58 & 0x100) is goal 0 natively (no social game). Clicks on a mode-4 ally that is in
    no pack engaged (npc 15 of an allied Polkeetimus: engaged=15, the host ran a recruit game "social mode 4").
  * Before `social.partner_relation` (build aeaedfc3 + the pmode tool): a guest's ordinary impress win on a neutral Trigo
    Morts (npc 25, relation 3, host mode 2) ran as a recruit game: host 02:57:55 "guest_pack: player 1 recruited
    221F38E0", "won a recruit game (social goal 4) with npc 25: it joins that player's pack", "impressed a creature (+0
    DNA)". `coop.social pmode`: host relation 3 / mode 2, inside the party relation 6 / mode 4 for every NPC (6 of 6).
    That recruit itself worked end to end: on the guest the Pack panel showed the Trigo Morts portrait and its card
    "Impress 1/3" (out/qa-shots/g_recruit_follow.png); the host listed it `pack`, `coop.gpack` "party 1: ... members 1";
    the guest walked ~13 m and the creature followed to 2-4 m of it (host positions 284.8,-417.4 -> 271.6,-427.9 with
    the guest at 273.2,-426.2).
  * With `social.partner_relation` on: pmode for neutral NPCs partner_mode=2 (allied host-species ones 4); the guest's
    win on Trigo Morts npc 30 (two sing prompts): host 03:16:09 "MSI impressed a creature (+12 DNA)", recruits=0, the
    creature impressed and not in a pack (native: impress first). After `coop.social record 067C92C6 6 3` (the species
    allied) npc 30 (impressed, allied): pmode partner_mode=4, the guest's card "The Trigo Morts are your allies! ... add
    them to your pack" (out/qa-shots/g_try5.png), and the guest's games with it ran as recruit games on the host
    ("social mode 4, impressed 1", 03:24:34, 03:38:37, 03:49:54). Those games were lost (prompts of a kind the test
    creature lacks: needs 3-4 points of pose); a won recruit game with the fix on was not reached (later
    clicks on that impressed ally only selected it, cursor kind 15 "+", engaged stayed -1: not analysed; ~10 tries). The
    recruit chain after a win (AddMember in the partner's party, follow, the guest's Pack panel) is the one proven in
    the bullet above; the fix changes only which goal the host picks.
  * Native reference (host, MSI): clicking an allied creature shows the "+" cursor and opens the social meter (green face,
    rounds 3): a native recruit is a prompt game too (out/qa-shots/h_native_ally_click5.png).
  * Off-run of `social.partner_relation` = the "Before" bullet above.

- Live QA 2026-11-21, third run (fix/l47-tribe 3c2a6550 + the BUG-9318 follow-ups, `social.partner_relation` on, MSI two
  games, Creature Vandh; host MSITribeHost instance 2, guest MSIGuest instance 1): PASS, a partner's recruit game won end
  to end. How the prompts became answerable: the guest's creature has the social kinds 1 / 2 / 4 (`coop.social abil -1`:
  sing key 1 1 pt, dance key 2 2 pts, charm key 4 2 pts); the host's own nest family (Clop 291EE5B5, npcs 22 23 24 28
  30, "family", in no pack) is relation 6 / partner_mode 4 for the guest (`coop.social pmode 22 1`) and its prompts are
  keys 3 / 4 / 1 / 2, so three of four prompt kinds are the guest's. The guest was put beside the host's nest
  (`coop.abilities put`), engaged npc 22 with a real click in social stance (engage.py) and answered each prompt with
  its own ability of the prompt's kind (play_social.py, `coop.social act`). Host log 11:41:24 "player 1 answers npc 22:
  type 00000023 kind 4 to prompt 0 ...: match, score 2.0 of 3.0", 11:41:33 "... kind 2 to prompt 1 ...: match, score
  4.0 of 3.0", "guest_pack: player 1 recruited 23A68370", "social: player 1 won a recruit game (social goal 4) with npc
  22: it joins that player's pack (decision 7L)"; `coop.social` recruits=1 recruits_refused=0; `coop.gpack` "party 1:
  ... members 1  23A68370 flags=00000181 species=291EE5B5"; `coop.npc list` #22 "pack family impressed=1". The guest's
  Pack panel shows the Clop's portrait (out/qa-shots/bug9308_guest_recruit_won.png). Follow: the guest walked about 30 m
  (`coop.abilities walk 205 -460 -70`): npc 22 went from (232.3 -445.8 -61.1) to (206.1 -455.9 -74.9), about 6 m from the
  guest at (204.6 -458.3 -69.7), and stayed there (bug9308_guest_recruit_follows.png).
  The neutral case (a win gives DNA, no pack) is the second run's "+12 DNA" line above (not repeated).
  Test pitfalls of this run: most wild species of Vandh were hostile (relation 5) to the party in this save (a herd of
  065CB7CB killed the guest twice even after `coop.social record 065CB7CB 6 0`); `coop.npc hosthit` kills count as the
  party's kills (relation events); a `record` of relation 6 on a species nobody plays is set back to 0 by BUG-9244's
  cleanup within minutes.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on fix/l47-impress (built, not run in game).
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: second live run; `social.partner_relation` (every partner win was a recruit game); see Fix and Verification.
- 2026-11-21: third live run (fix/l47-tribe): a partner's recruit game won end to end, the creature joins and follows.
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
