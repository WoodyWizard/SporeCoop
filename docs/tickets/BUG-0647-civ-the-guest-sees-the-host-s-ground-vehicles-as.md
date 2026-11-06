---
id: BUG-0647
title: Civ: the guest sees the host's ground vehicles as creature-like monsters (the host's vehicle designs never reach it)
status: open
kind: bug
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-09, M0ng0r-15
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0424, BUG-0469
---

## Report
Discord report 2026-11-06 (0.7.8): the host (discord-2026-11-06/Sterben-09, s 5930) "the guest doesn't see vehicles but
abominations"; the guest (M0ng0r-15, s 5962) "I see literal nightmarish monsters as ground vehicles". The guest's shot
at s 5930 shows two big creature-like models labelled with the host's name next to an enemy city, where the host had
its 2 vehicles.

## Root cause
Observed (logs): guest `civ(guest): mirrors=2 created=2` at s 5930, host `vehicles=2` at s 5910; no "cannot be built
here" line (the BUG-0469 fallback); the only creations the host sent at the Tribe -> Civ change were two creatures
(291825D4, 291825C4, "own creation ... to player 1"); the mod has no vehicle / building creation transfer
(docs/re/creations.md).
Inferred: the mirror is built from the host civ's design key (0x2918xxxx from the host's own editor saves), which the
guest's game never received; the guest's own creations use the same key range (own creature 29182710, 202 own
creations), so the key resolves to another creation of the guest's (the instance collision in docs/re/creations.md) or
the missing model falls back to something else. Class BUG-0424 (content the other game lacks).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: host and guest with their own profiles; the host makes its vehicle in the Tribe -> Civ editor; compare the
     mirror's design key and model resource on the guest with the host's. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
