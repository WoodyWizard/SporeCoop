---
id: BUG-0179
title: Creature (guest): the social game is simplified
status: open
kind: feature
stage: Creature
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-11-03: reported again (0.7.3.4 forum post and a 0.7.3.6 direct message): the guest cannot befriend / socialize in the Creature stage (animals do not want to be friends). Still open; a top Creature complaint.
- 2026-11-03: fix/w40-dna-social measured it with the relations fix (BUG-0462) merged: the social path works for every socializable species of the fixture by key, but a CLICK on the social ability's button did nothing for a guest: BUG-0518 (fixed, social.bar_click). Still open here: the meter and the NPC's prompts on the guest, any social ability counts as the answer, 0x60B4123 not posted.
