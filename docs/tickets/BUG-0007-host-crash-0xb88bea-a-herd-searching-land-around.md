---
id: BUG-0007
title: Host crash 0xB88BEA: a herd searching land around a home inside the planet
status: fixed
kind: crash
stage: Creature
reported: 2026-10-25
source: Spore Exception Report (host, 13:36, 0.7.2.1)
role: host
versions: 0.7.2.1
fixed_in: 0.7.2.3
branch: fix/creature-ai-crash
commits: 70a9d14, d822936, e54220a
tests: editor_strays
signature: dc04aee5:0xB88BEA read past the planet cube map (Planet_NearestCell)
docs: docs/re/creature.md 10
related:
switch: core.strays
---

## Report
The host crashed in an NPC behaviour tree tick.

## Root cause
A partner puppet spawned from a blended snapshot taken outside the world got a herd home deep inside the planet; after the editor the old puppet stayed as an unregistered NPC whose land search read before the map.

## Fix
No blending across a mode/world change; left-over puppets removed after the editor; a puppet herd keeps its home under the puppet.

## Verification
coop_editor_strays.py FAIL before (homes inside the planet, left-over puppets), OK 3x after.

## History
- 2026-10-25: reported (Spore Exception Report (host, 13:36, 0.7.2.1)).
- 2026-10-25: fixed on fix/creature-ai-crash, released in 0.7.2.3.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: core.strays (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
