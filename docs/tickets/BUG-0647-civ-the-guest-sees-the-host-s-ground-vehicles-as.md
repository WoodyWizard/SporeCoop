---
id: BUG-0647
title: Civ: the guest sees the host's ground vehicles as creature-like monsters (the host's vehicle designs never reach it)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-09, M0ng0r-15
role: guest
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-tribe-civ
commits: 142b991e
tests: civ_host_design
signature:
docs: docs/re/civ.md#civ-host-designs
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

Observed 2026-11-07 (lab, fix/w46-tribe-civ): with another vehicle model under the host's design id in the guest's My Creations, the guest's mirror of the host's vehicle was built from the guest's own model (its bem hash 76023C9D, the host's DD673F46), for a new vehicle and for a vehicle of the loaded world the guest binds. The key is the host civ's design key (cCivilization::GetDesignKey, the same key in the guest's copy of the save); the vehicle's model key is at +0xC4.

## Fix
`creations.host_designs` (BUG-0647): the guest asks the host for every vehicle design key a mirror needs; the host sends the records of a design of its own My Creations (or says it is not its own); the guest registers them under a key of its own (0x7D......, as the host's own species) and builds the mirror from it; a bound vehicle of the loaded world is re-keyed (SetModelKey, vt 0x94 of +0x34). No mirror is made while the answer is out (20 s at most). Off: the design key as it is.

## Verification
Test civ_host_design (coop_civ_host_design.py): the host's and the guest's My Creations hold different vehicle models under one id that both civs name as their military land design; the guest's mirror shows the host's model (the bem the guest's game answers for the mirror's model key), new and after a world load.
- FAIL (SPORE_FIX_OFF=creations.host_designs): out/lab/20261107-074609 (the mirror shows 76023C9D, the guest's own).
- PASS: out/lab/20261107-074448, out/lab/20261107-074935.
- Not verified: the look on the screen (the check is the model record the game answers); buildings: BUG-0670.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: fixed in fix/w46-tribe-civ (creations.host_designs).
- 2026-11-07: released in 0.7.10.
