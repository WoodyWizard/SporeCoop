---
id: BUG-1252
title: Creature: a hunger thought bubble over the guest's puppet in the host's game
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: host
versions: <0.7.15
fixed_in: unreleased
branch: fix/w53-puppet
commits: 6750392e
tests: puppet_hunger (tools/tests/coop_puppet_hunger.py)
signature:
docs: docs/re/creature.md#creature-puppet-hunger, docs/06-testing.md#testing-puppet-after-social
related: BUG-1241, BUG-0451
---

## Report
A players' video: a thought bubble with a gnawed bone hangs over the guest's puppet in the host's game for a long time, while the guest's own creature is not hungry. Suggested cause: the puppet object's own hunger on the host lives its own life.

## Root cause
* Observed (Ghidra): the hunger bubble is `SporeCoop::Creature_ThoughtBubbleUpdate` 0xE8BF00's pool-2 effect under key
  0x062EDBA6 (0xDE4F1682 for a carnivore: the gnawed bone), started while the creature's mood (+0xCC0, 0xC02F50) is 2
  or 3 (hungry) and stopped (StopEffectFromPools) when the mood changes.
* Observed (lab 20261118-123004): the suggested cause is wrong. The host's puppet has no hunger of its own: its meter
  does not change (`hunger_delta=0`), its mood is never computed (`mood=0`), and set to 10 it shows no bubble. The
  bubble over the puppet is the GUEST's own: the guest's game shows it over its own creature when its meter runs low
  (100 to empty in 5 minutes, mood 2 under the threshold) and the effects relay starts it on the puppet (BUG-0451); when
  the guest eats, its game stops it and the relayed stop removes it.
* Observed (lab 20261118-124058, -123615): the bubble stays on the puppet when that stop is lost. effects.cpp took the
  key out of its list of sent keys and THEN applied the rate limit (40 a second): a stop that met an empty bucket (a
  burst of effects, a fight's hits) was dropped and never sent again, the host's puppet kept the bubble for good
  (`coop.effects` on the host: stopped=0; key frame: the bubble over the guest's creature on the host's screen only).
* Inferred: in the video the guest had been hungry (its own game showed the bubble too) and ate while effects came in
  a burst, or its stop was dropped otherwise by the limit. Not seen: the video's own moment.

## Fix
A stop of a pool effect of the player's own creature is never dropped by the rate limit (it takes a token when there
is one and goes anyway; at most one stop per started key). Switch `coop.fix effects.stop_always off` (0.7.15);
`coop.effects` prints `stops_past_rate=`. The first idea (keep the puppet's meter full, `creature.puppet_full`) was
removed again: the puppet has no hunger.

## Verification
Test `puppet_hunger` (creature fixture): the host's puppet's meter at 10 (no bubble), the guest's own meter at 10 (its
bubble on both games), then the guest eats during a burst of effects every frame with the rate limit in debt as such
a burst leaves it (`coop.effects load 40 10 E1A4BD93`, `coop.effects drain 10`): 4 s after the burst the host's
puppet must have no bubble (`coop.effects map` active).
- FAIL with SPORE_FIX_OFF=effects.stop_always: out/lab/20261118-124058 (lane 4; 9 samples with the bubble on the host's
  puppet only; key frame: the bubble over the guest's creature on the host's screen), out/lab/20261118-123615 (the
  test's earlier shape).
- PASS: out/lab/20261118-124118 (lane 5, `stops_past_rate=1`, no bubble in the key frame).
- With the test's earlier burst every 50 ms the stop found a token at the lab's frame rate and the fix-off run passed
  (out/lab/20261118-123755): hence the drain.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-puppet): the puppet's own hunger ruled out (measured); the bubble is the guest's own, kept on the puppet by a stop the rate limit dropped; fixed (effects.stop_always).
