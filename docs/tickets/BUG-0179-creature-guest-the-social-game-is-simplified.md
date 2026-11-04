---
id: BUG-0179
title: Creature (guest): the social game is simplified
status: fixed
kind: feature
stage: Creature
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.5
branch: fix/w41-creature-left
commits: 50030f4a 47779e7b 
tests: creature_social_meter (coop_creature_social_meter.py), creature_social_bar
signature:
docs: docs/re/creature.md#creature-guest-meter
related: BUG-0518, BUG-0525
---

## Report
A list of limits and open ends of a feature, kept as one ticket.

Copied from docs/known-issues.md, where the entry stays up to date:

* Since 2026-10-23 a guest befriends a mirrored NPC (click in the social stance, a social ability
  key; the host applies the native win, docs/re/creature.md 8.3), but the guest does not see the
  native social meter and the NPC's ability prompts (they belong to the NPC's behaviour tree, which
  runs only on the host): any social ability counts as the answer, one press per round. A click on
  the ability bar instead of a key is not counted (the mod polls the keys 1-4). The host's own
  game is native. The success message 0x60B4123 (CreatureStageEventHandler) is not posted for a
  guest's win. Reproduce: `tools/tests/coop_creature_social.py`.

## Root cause
Observed (Ghidra, docs/re/creature.md#creature-guest-meter): the game's social meter is opened by the NPC's BT node SocialPropose 0xDA4360 and the win message 0x60B4123 is posted by CreatureSocialSuccess 0xD9BBD0; both run only where the NPC's behaviour tree runs (the host), so a guest's game with a mirror never showed the meter and never posted the message. Observed (Ghidra): 0x60B4123 makes CreatureStageEventHandler roll the impressed species' part reward; inferred (not seen at runtime, the roll is random): a guest's friends never gave it a part.

## Fix
social.cpp social.guest_meter: the guest drives the game's own meter object ([[cCreatureModeStrategy +0x68] +0x60]) as SocialPropose does (Reset, SetVisible, rounds from the mirror's tuning, relation face) when it engages a mirror, moves bar 0 at each round the host counts, at its win sets it full, Finish, and posts 0x60B4123 {avatar, proxy, 0} on its own game, hides it when the engagement ends. Switch coop.fix social.guest_meter off. Still open, split into BUG-0525: the NPC's prompts and 'any social ability counts as the answer'.

## Verification
creature_social_meter (tools/tests/coop_creature_social_meter.py), real input on the guest (social stance, click on a Kook, clicks on the sing button), checks the guest's game: the meter layout's own visible byte is 1 when the host's win arrives and 0 after, rounds on the meter, the guest's CreatureStageEventHandler got 0x60B4123.
- FAIL with SPORE_FIX_OFF=social.guest_meter: out/lab/20261103-234903 (no meter opened, not visible at the win, no 0x60B4123).
- PASS: out/lab/20261103-234748.

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-11-03: reported again (0.7.3.4 forum post and a 0.7.3.6 direct message): the guest cannot befriend / socialize in the Creature stage (animals do not want to be friends). Still open; a top Creature complaint.
- 2026-11-03: fix/w40-dna-social measured it with the relations fix (BUG-0462) merged: the social path works for every socializable species of the fixture by key, but a CLICK on the social ability's button did nothing for a guest: BUG-0518 (fixed, social.bar_click). Still open here: the meter and the NPC's prompts on the guest, any social ability counts as the answer, 0x60B4123 not posted.
- 2026-11-03: meter and win message fixed on fix/w41-creature-left (social.guest_meter); prompts split into BUG-0525.
- 2026-11-04: released in 0.7.5.
