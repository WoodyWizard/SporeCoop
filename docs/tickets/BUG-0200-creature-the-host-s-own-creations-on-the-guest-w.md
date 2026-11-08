---
id: BUG-0200
title: Creature: the host's own creations on the guest, what is still open (2026-10-25)
status: fixed
kind: bug
stage: Creature
reported: 2026-10-25
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: unreleased
branch: fix/w46-p4-creature
commits: 412db52c
tests: nest_species (tools/tests/coop_nest_species.py, extended: the species profile the guest's game gives for the host's key, and its own again after the session)
signature:
docs: docs/re/creations.md#creations-species-map, docs/known-issues.md (Creature: the host's own creations on the guest)
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
Observed (fix/w46-p4-creature, 2026-11-07): the guest's game answers every lookup of a species by key from its
species manager's map (cSpeciesManager +4, one profile per key for the whole process; Ghidra GOG 2024:
GetSpeciesProfile 0x4DF5C0, docs/re/creations.md#creations-species-map). For a host creation whose id the guest's
own My Creations also have, that profile is the guest's creation: in nest_species (the guest's Kirgle under the
key of the host's TigerHorse 290229B9) `coop.creations speciesof 290229B9` on the guest gave profile key 290229B9
with the guest's model (bem 9B6134E2, the host's 6E1A058F), before and after the world (with the herd of that
species) went to the guest again. The save's herds and nests of the host's creature, and every card the game
builds from them (the nest's "The <name> are your allies!"), read that profile. The mod's own NPC proxies already
used the host's records under a key of ours (0x7D......), so only what the game itself looks up by the host's key
was wrong. Rewriting the save to our key lost the herds (docs/re/creature.md 15), so the key must stay.

## Fix
`creations.host_key_alias` (creations.cpp alias_tick): for each host creation the guest registered under a key of
ours, the node of the host's key in the guest's species manager map holds the profile of our key (the host's
records), so every lookup by the host's key gets the host's creation; the node's earlier value goes back at the
session end. Not for the keys players play (the guest's avatar, the world's player species, the host's creature):
SetSpecies would write our key into the avatar and a key of ours must never reach a save; those follow the bundles
as before. `coop.fix creations.host_key_alias off`: the guest's own creation of that key (0.7.10). The real-nest
case (a herd with an archetype) needs no separate path: a nest's herd looks its species up by the same key.

## Verification
FAIL: `SPORE_FIX_OFF=creations.host_key_alias` nest_species out/lab/20261107-210444 (the guest's profile of
290229B9: key 290229B9, bem 9B6134E2 = its own creation, both before and after the world went again).
PASS: out/lab/20261107-210110 nest_species#1, #2 (profile of 290229B9 = key 7D851520, bem 6E1A058F = the host's),
out/lab/20261107-210644 (also: after the guest left, its game gives its own creation again: restored 1).

## History
- 2026-10-25: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-creature (wave 3): no change; still open. The remaining part (a card built from a nest names the guest's creation of the host's key) needs the nest's species to resolve to the guest's local key, and rewriting the save lost herds before (docs/re/creature.md 15): a redesign of how the host's creations are keyed on the guest, left for a dedicated task. The real-nest check needs a fixture planet with a host creation's nest.
- 2026-10-30: fix/w13-creature: no change; still open. Both remaining parts need what wave 3 said: the nest card needs the nest species of a host creation to resolve to the guest's local key (the save rewrite lost herds before, docs/re/creature.md 15), and the real-nest check a fixture planet with a nest of a host creation. Not a patch-release change.
- 2026-11-07: fix/w46-p4-creature: fixed (creations.host_key_alias): the guest's species manager answers for the host's keys with the host's creation; nest_species FAIL/PASS.
