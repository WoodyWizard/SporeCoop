---
id: BUG-1353
title: 0.7.16 final check: tag_cinematic judged the tag inside the guest's CRG_ClaimNest, which Enter did not end
status: open
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in:
branch: fix/w60-dna
commits:
tests: tag_cinematic (twin tag_cinematic_off)
signature:
docs:
related: BUG-0985, BUG-1185
---

## Report
rel/0.7.16 worktree out/lab/20261120-041304 tag_cinematic FAIL "after: drawn on the guest [] (cinematic 63B7060A)".

## Root cause
A test bug: 40 s of Enter did not end the guest's CRG_ClaimNest (tag_cinematic.guest.game.log: busy=cinematic,
input_lock=1 up to s 60); no tag was drawn because the cutscene still ran (find.cinematic right). Known game behaviour:
docs/re/creature.md ("CRG_ClaimNest started with PlayCinematic does not end with Enter on the guest; EndCinematic ...
ends it"), the same failure in 20261115-225537 (feat/w51-harness, BUG-1185) and 20261117-154911 (rel/0.7.15);
coop_puppet_after_events.py and coop_chat_caption.py note / use the same fallback.

## Fix
coop_tag_cinematic.py: Enter for up to 40 s as before, then the caption OK's own call (cCinematicManager_EndCinematic)
and up to 10 s for the end; the log says how it ended; still playing: INCONCLUSIVE (not PASS). The before / during /
after checks unchanged.

## Verification
Open, waiting for the lab run. PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run tag_cinematic --lane 2 --retry 0 --no-build` (log "the cutscene ended by
..."); FAIL (twin, new suite line): `SPORE_DETECTOR=20 python3 tools/lab.py run tag_cinematic_off --lane 2 --retry 0 --no-build`; key frame 01: the cutscene
without a tag.

## History
- 2026-11-20: opened.
