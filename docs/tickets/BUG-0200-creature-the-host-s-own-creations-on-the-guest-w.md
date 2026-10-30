---
id: BUG-0200
title: Creature: the host's own creations on the guest, what is still open (2026-10-25)
status: open
kind: bug
stage: Creature
reported: 2026-10-25
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w13-creature
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Since fix/nest-species a guest builds the host's NPCs of the host's own creations from the host's
  records under a key of its own (0x7D......, docs/re/creature.md 15). The host's world on the guest
  still names them by the host's key, which is the guest's own creation there: a card the guest's
  game makes from a nest (not from a creature) shows the guest's creation's name (with the host's
  relation). Rewriting the save to the guest's key lost those herds at the load and crashed the
  pack panel (section 15 there). Both players need this version: an older host sends nothing (the
  guest shows its own creation of the key, as before), an older guest drops kind 5.
* Not checked: a real nest (with an archetype) of a host creation on the guest (the fixture planet
  has Maxis species only).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-25: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-creature (wave 3): no change; still open. The remaining part (a card built from a nest names the guest's creation of the host's key) needs the nest's species to resolve to the guest's local key, and rewriting the save lost herds before (docs/re/creature.md 15): a redesign of how the host's creations are keyed on the guest, left for a dedicated task. The real-nest check needs a fixture planet with a host creation's nest.
- 2026-10-30: fix/w13-creature: no change; still open. Both remaining parts need what wave 3 said: the nest card needs the nest species of a host creation to resolve to the guest's local key (the save rewrite lost herds before, docs/re/creature.md 15), and the real-nest check a fixture planet with a nest of a host creation. Not a patch-release change.
