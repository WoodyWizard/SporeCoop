---
id: BUG-9258
title: Creature guest: a lost social game says 'It does not want to socialize now', the same as a refusal
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: player report (SporeCoop-report-20261118-215146.zip)
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in: 0.7.14.1-lineages66-dev
branch: feat/lineages-47
commits: 80be5f2f, c3ba2fbe
tests: tools/qa_win/qa.py (manual: two MSI games, coop.social act)
signature:
docs:
related: BUG-0525, BUG-9230, BUG-9212
---

## Report
A guest (BigTexas, 65-dev): "the creature says 'It does not want to socialize.' Sometimes it lets him start, but the
animation for the pink bar isn't smooth, then it randomly stops working and says 'It does not want to socialize.' again."
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed 2026-11-18 on two MSI games (host Samuth, guest with its own creature; diagnostic lines added to the host's
social.cpp): the host ends a guest's social game in two ways and the guest showed the same line for both. (1) Refused:
the species does not socialize (CreatureSocialMode 0: relation 5 aggressive, e.g. after the party killed some of it,
or 6 allied already); host and guest agreed on every species of the fixture. (2) Lost (BUG-0525's prompts): the list
of the creature's moves ran out before the bar was full. With only Sing, 6 of 9 answers did not match the move asked
(0.1 of the points each), so every game was lost. Inferred: BigTexas' "randomly stops" is (2). The bar moved in steps
(each answer confirmed by the host one round trip later), not smoothly as in single player.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
social.cpp: a lost game (the result's fail animation 0x53685D8F) says "It lost interest: copy the move it shows with
the same ability to fill the bar"; a refusal keeps "It does not want to socialize now". The host logs every "no" with
its reason (social mode, distance), 40 lines a session; the guest logs which kind arrived. The pink bar glides to each
answer's value (a fifth of the way every 30 ms) instead of jumping when the host's answer arrives. Switch:
`coop.fix social.meter_glide off` (the bar jumps as before).

## Verification
no-fail-run: a message text and an animation. Seen on the MSI pair 2026-11-18: refusals logged as 'an act while the
npc does not socialize (social mode 0)', lost games as 'the prompt list is through and the meter not full', the new
"It lost interest" line shown; the bar glided (30 fills for 6 answers). A won social game was not reached (the test
creature has only Sing; the prompts asked for others, as in single player).

## History
- 2026-11-18: opened.
- 2026-11-19: the bar glides; fixed on feat/lineages-47 (c3ba2fbe).
