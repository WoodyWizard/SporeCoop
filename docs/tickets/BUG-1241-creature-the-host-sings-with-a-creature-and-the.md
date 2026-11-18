---
id: BUG-1241
title: Creature: the host sings with a creature and the other player's creature starts the sing animation too and keeps it (rings effect) while it runs about, without end
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video
role: guest
versions: <0.7.15
fixed_in:
branch: fix/w52-evoeditor
commits:
tests: social_sing, social_sing_partner, social_sing_own, social_sing_own_walk, social_sing_own_flock, social_sing_reverse, social_sing_own_lowfps, brain_rings
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
* Second round (2026-11-18, the coordinator's paths), 0.7.15, the guest on its OWN creation from My Creations
  (290225CD, the host 065CBE70; the players' default): the host sings (out/lab/20261118-104317 x2), the guest walks next
  to the host while it sings (-104320, -10494x), the host's pack from its Summon Flock stands by (-104323, -10494x),
  the guest sings and the host is the other player (-104326 x2): the other player's creature never played a social
  animation on either game, its puppet only its own locomotion; no pool effect left. Both games at 10 fps
  (`social_sing_own_lowfps`, -104713, -10494x): INCONCLUSIVE, the host's social game never started in 40 s.
* The same tests on v0.7.14.1 (a scratch worktree SporeCoop-wt/tag/v0.7.14.1-w52evo built from the tag, the 0.7.15
  tests copied into it, out/lab/20261118-104716 of that tree): social_sing, social_sing_own, social_sing_reverse OK.
  So no 0.7.15 change to bisect.
Not tried: the mating call (its green ring), the guest engaged in a social game of its own when the host sings, a
long session (hours) before the song.

## Fix
None: not reproduced. The tests stay as a guard (`social_sing` checks that the guest's creature never plays the
host's social animations or keeps a pool effect its own game does not have).

## Verification
no-fail-run: not reproduced on v0.7.15 (runs above); the tests' other failures are BUG-1244 and the test's own
flakiness (the host's song starts in about half of the runs, the guest put by the nest rocks cannot run).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w52-evoeditor): not reproduced in 12 runs of social_sing / social_sing_partner / brain_rings; known-issues entry.
- 2026-11-18 (fix/w52-evoeditor): second round (own creature, walk, pack, reverse, 10 fps, v0.7.14.1): still not reproduced.
- 2026-11-18: a second analysis of the video: the gold sing rings come from the GUEST's puppet (seen in the host's game) for about 3 minutes (also while the guest runs, fights or stands with its mouth shut), and again later for half a minute; the guest had been befriending creatures by singing (one song befriended a level-1 creature). Suggested causes to check: social::host_act calls cCreatureBase::PlayAbility on the guest's puppet in the host's game (the puppet has no AI tick to end the ability); StartOrStopEffectFromPool1/2 stops are not hooked (only StopEffectFromPools); puppet pool effects (1/2/3) have no 8 s auto stop like world effects (5); a single stop in effects.cpp drops the key from g_sent_keys before the rate limit and goes through send_event. Reopened.
