---
id: BUG-9308
title: Creature (guest): a creature a partner wins in a social game never joins the partner's pack
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: decision 7L (owner, 2026-11-20) and the 67-dev guest report 20261120-205700
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-impress
commits:
tests:
signature:
docs: docs/re/creature.md#creature-native-recruit
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

## Verification
no-fail-run: pending the lead's two-player QA (built only).

## History
- 2026-11-20: opened.
- 2026-11-20: fix on fix/l47-impress (built, not run in game).
