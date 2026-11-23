---
id: BUG-1244
title: Creature: the host's sing in a social game with an animal is not shown on its creature in the guest's game (042A1CAF never on the puppet)
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: test: social_sing (fix/w52-evoeditor)
role: host
versions: 
fixed_in:
branch: fix/w76-motion
commits: de506908
tests: social_sing, social_sing_bhv_off (twin)
signature:
docs:
related: BUG-1241, BUG-1242, BUG-0891, BUG-1253
switch: creature.avatar_bhv_anims
---

## Report
Found by `social_sing` (tools/tests/coop_social_sing.py, creature fixture, fix/w52-evoeditor, while looking for
BUG-1241): the host clicks a Kook in the social stance and presses 1 until its creature sings. The host's own creature
plays the sing ability's animation 042A1CAF (`coop.npc cur`, `cur avatar`) and B4212A07, 04866D8E, 04866DAB; the
guest's game shows the host's puppet with 04866D8E / 04866DAB (relayed) but never 042A1CAF, sampled about every
0.4 s for 5.5 s, in every run where the host sang: out/lab/20261118-095915 social_sing, out/lab/20261118-101227
social_sing#1 and #3, out/lab/20261118-102243 social_sing#1 and #3. The opposite direction (the guest's sing on the
host's puppet of it) is shown (social_console, BUG-0891).
How to reproduce: `lab.py run social_sing --lane N`, read "the guest's game shows the host singing (its puppet)".
Not analysed: whether the host's ability animation reaches PlayAnimation's hook (on_anim) at all, or is played and
overwritten on the puppet at once by the walk driver; the test's sampling could miss a very short animation.

## Root cause
The same as BUG-1253 (fixed in 0.7.15.2, 7d01ccd6, `creature.avatar_bhv_anims`): an animation the player's own
creature plays by itself after a click (eating a body, the sing of a social game) was never relayed to its puppet on
the partner's game. Observed in the lab's own logs (out/research/agy-review-misc.md, static checks): the test's old
diagnostic line "the guest's game shows the host singing: X; BUG-1244" printed False in all 13 logs before BUG-1253
(0.7.14.1-w52evo 20261118-104716, fix/w52-evoeditor 20261118-104317..104944, rel/0.7.15.1 20261118-110806 and
-121937) and True in all 26 since (rel/0.7.15.2 20261118-134852 / -144132, rel/0.7.16 20261119-214823,
20261120-041304, 20261120-133703, `social_sing_own_lowfps` included; 19 host, 7 guest `--reverse` logs).

## Fix
The game code is BUG-1253's (0.7.15.2). 2026-11-23 (fix/w76-motion, idea from fix/agy-open-bugs fc420dcb): the test
checks it now. coop_social_sing.py: when the singer sang, "the <other>'s game shows the <singer> singing on its
creature" is a check (a social animation in the other game's samples of the singer's puppet, `coop.npc cur`), not a
print; every variant where a song happened runs it (social_sing, _own, _own_walk, _own_flock, _reverse,
_own_lowfps, _partner when a Kook behind the partner sang; in --reverse it checks the guest's song on the host's
puppet, the BUG-0891 path). New suite twin `social_sing_bhv_off` (SPORE_FIX_OFF=creature.avatar_bhv_anims) must
FAIL on that line.

## Verification
Open, waiting for the lab run (pending night run: social_sing social_sing_bhv_off):
`SPORE_DETECTOR=20 python3 tools/lab.py run social_sing --repeat 2 --lane N --retry 0 --no-build` must PASS and
`SPORE_DETECTOR=20 python3 tools/lab.py run social_sing_bhv_off --repeat 2 --lane M --retry 0 --no-build` must FAIL
on "the guest's game shows the host singing on its creature". Equivalent evidence already on disk: the 13 / 26 logs
of the Root cause.

## History
- 2026-11-18: opened.
- 2026-11-23 (fix/w76-motion): the check is an assertion now (idea from fix/agy-open-bugs fc420dcb); twin social_sing_bhv_off.
