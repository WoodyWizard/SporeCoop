---
id: BUG-1353
title: 0.7.16 final check: tag_cinematic judged the tag inside the guest's CRG_ClaimNest, which Enter did not end
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w60-dna
commits: 48dfc9fa
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
Was waiting for the lab run. PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run tag_cinematic --lane 2 --retry 0 --no-build` (log "the cutscene ended by
..."); FAIL (twin, new suite line): `SPORE_DETECTOR=20 python3 tools/lab.py run tag_cinematic_off --lane 2 --retry 0 --no-build`; key frame 01: the cutscene
without a tag.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tag_cinematic PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643; the twin tag_cinematic_off FAIL in 20261120-130608 ("during the cutscene no partner tag or arrow is drawn"). Closed.

## History
- 2026-11-20: opened.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
