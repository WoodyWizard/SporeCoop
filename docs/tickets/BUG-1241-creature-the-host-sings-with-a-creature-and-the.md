---
id: BUG-1241
title: Creature: the host sings with a creature and the other player's creature starts the sing animation too and keeps it (rings effect) while it runs about, without end
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video
role: guest
versions: <0.7.15
fixed_in:
branch: fix/w52-evoeditor
commits:
tests: social_sing, social_sing_partner, brain_rings
signature:
docs: docs/known-issues.md#issue-partner-sings-along, docs/06-testing.md#testing-evo-screens
related: BUG-1240, BUG-1243, BUG-1244, BUG-0451
---

## Report
A players' video (players on a version before 0.7.15), seen by the owner: the host starts singing (the social
stance) with a creature, and the other player's creature starts the sing animation too and keeps it, with the rings
effect, without end while it runs about.

## Root cause
Not found. What was measured (creature fixture, the guest 3-4 m from the host, both playing the world's species as
after BUG-1240; `coop.npc cur` reads the game's own current animation of the own avatar and of every puppet on both
games every ~0.4 s; `coop.effects map` / `probe` read the creatures' pool effects; key frames):
* The host engages a Kook (a click in the social stance) and presses the sing key until its creature sings
  (042A1CAF on the host's avatar in 5 of 9 runs): neither the guest's own creature nor the host's puppet of it played
  a social animation, during the song, while the guest ran (W) afterwards, or 20 s later (out/lab/20261118-095915,
  -101227 x3, -102243 x3, -102852). The puppet showed only animations the guest's own game played.
* The host sings to the guest's creature (a click on its puppet, `social_sing_partner`): no social game starts with a
  puppet, nothing plays on it (out/lab/20261118-100537, -102852).
* The host's game keeps the guest's puppet in a nestless herd of the mod's (not the avatar's herd), without the pack
  bit (`pack=0 family=0`), so neither the pack nor the family takes up the host's song.
* The social win's pool effect D7BE955C (key 02C4C076) starts on each player's own creature (the shared DNA) and on
  each puppet (the effects relay); it was inactive on all four 30 s later; no stop message is relayed, the effect ends
  by itself. The brain upgrade of the shared DNA (`brain_rings`, captions confirmed 8 s apart, the guest running
  after it): no puppet kept any effect of it (out/lab/20261118-101846).
* Found on the way: the host's sing is not shown on its puppet in the guest's game at all (BUG-1244).
Not tried: a guest with a creation of its own (another species), a pack member or an ally next to the players, the
guest in the social stance or engaged itself when the host sings, the mating call (its green ring), a guest at low fps.

## Fix
None: not reproduced. The tests stay as a guard (`social_sing` checks that the guest's creature never plays the
host's social animations or keeps a pool effect its own game does not have).

## Verification
no-fail-run: not reproduced on v0.7.15 (runs above); the tests' other failures are BUG-1244 and the test's own
flakiness (the host's song starts in about half of the runs, the guest put by the nest rocks cannot run).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w52-evoeditor): not reproduced in 12 runs of social_sing / social_sing_partner / brain_rings; known-issues entry.
