---
id: BUG-1412
title: creature_sneak: the observer's game drew the partner's puppet see-through before any sneak (model alpha 0.13-0.2, +0xB7C 1.0); the sneak look was compared on it
status: fixed
kind: test
stage: Creature
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 creature_sneak
role: 
versions: 0.7.4, unreleased
fixed_in: unreleased
branch: fix/w62-flake-cc
commits: 1e99207e
tests: creature_sneak
signature:
docs: docs/re/creature.md#creature-sneak
related: BUG-0463, BUG-0989
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 4, cold): creature_sneak FAIL "guest: the other game draws the
puppet as the actor's game draws its own creature (alpha 1.0 / own 0.2)" and "still sneaking 2 s later ... own (1, 0.0,
0.2, 1000.0, 0.2), puppet (1, 0.0, 1.0, 1000.0, 0.2)"; the host's direction passed. Known flake (2 of 13 on rel/main);
rel/0.7.4 20261103-204638 failed the other way (host: puppet 0.2 / own 1.0).

## Root cause
Observed (the test's `coop.sneak` lines: sneak, noise, +0xB7C alpha, energy, model +0x80 alpha):
* before any sneak the guest's game drew the host's puppet with model alpha 0.13 while its +0xB7C was 1.0 and it did
  not sneak (`puppet 0: sneak=0 ... alpha=1.00 ... model_alpha=0.13 b58=800`);
* the host's game drew the guest's puppet with model alpha 0.2 and +0xB7C 1.0 while the guest sneaked AND after it
  stopped (`after sneak off: ... puppet (0, 6.0, 1.0, 1000.0, 0.2)`): the puppet sneaked (+0xBB1 1, noise 0, so
  BUG-0463's fix worked), but the game's sneak look (0xC1CF30 eases +0xB7C to 0.2 and writes the model alpha,
  docs/re/creature.md#creature-sneak) did not change its +0xB7C, and its model alpha did not follow the sneak either;
* in the host's direction the guest's game's puppet of the host eased +0xB7C and the model alpha together (0.2 / 0.2,
  then 0.85 / 0.85 after it stopped).
So on the failing side the puppet's drawn alpha was set by something else than the sneak, before and after it.
Inferred (not proven): Spore's camera fades a creature that stands between the camera and the avatar, and the
partner stands 5 m beside the avatar (BUG-0989); while faded, the sneak look does not run on it. Old, not new in 0.7.16;
w61 changed nothing of the sneak or the puppet's look.

## Fix
Test: before each direction the observer's puppet of the actor must be drawn opaque (model alpha >= 0.9) while nobody
sneaks; if not, the actor moves 6 m to another side of the observer (`coop.npc gopos`, up to 4 places) and the line
says so. The sneak checks are unchanged. If no place helps, a new check fails ("the other game draws the puppet opaque
before the sneak"): then the see-through puppet is a mod bug of its own.

## Verification
no-fail-run: test-only change. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_sneak --repeat 4 --lane N
--retry 0 --no-build` PASS; a run that prints "see-through before any sneak" confirms the cause (or refutes the camera
fade if moving does not help). Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): creature_sneak PASS 4 of 4 in 20261122-193406, and in 20261122-194210 and 20261123-023643. Closed.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); fixed in the test.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
