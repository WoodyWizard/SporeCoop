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
branch:
commits:
tests: social_sing
signature:
docs:
related: BUG-1241, BUG-1242, BUG-0891
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
