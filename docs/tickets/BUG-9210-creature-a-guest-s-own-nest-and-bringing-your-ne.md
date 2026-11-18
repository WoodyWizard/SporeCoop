---
id: BUG-9210
title: Creature: a guest's own nest (and Bringing your nest) comes minutes after it spawned: the host's new-world intro holds the nests
status: fixed
kind: bug
stage: Creature
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: d512a523 37806c28
tests: tools/qa_win (manual, 2026-11-15): t9210b (TITAN) and the MSI stage tour (Vandh.spo)
signature:
docs:
related: BUG-9167 BUG-9182 BUG-9208
---

## Report
Owner: "'Bringing Your Nest' only happened after the player spawned into the world."

## Root cause
Observed (host log): the world started at 02:18:43; the host's game was in the new world's intro (busy cinematic, input_lock, paused) until 02:22:39; home_nests::host_tick waits for no cinematic and no pause, so the seat nests came at 02:22:44-46, then the nest share and the guests' reload ("Bringing your nest"). The guests had spawned at 02:18:57 and 02:19:16 and played 4 minutes at the host's nest. Fix idea: guests stay behind their loading screen until their nest exists (or the nests are made before the host's intro ends).

## Fix
- The host makes a joining player's own nest before that player's world is shared: the share waits (at most 120 s) while the player's nest is due (creature.nest_before_world), and the nest maker no longer waits for a cinematic or pause.
- No hold while the host is dead (its death screen): no nest is made then, so the hold only cost 120 s (37806c28).

## Verification
FAIL (MSI tour, Vandh.spo, host on its death screen, before 37806c28): the share waited the full 120 s, the MSI loaded, then "Bringing your nest" loaded it again.
PASS (MSI tour, host alive): the MSI spawned at its own nest with no second load.
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
- 2026-11-15 (overnight tour): the host-dead case found on the MSI; no hold while the host is dead (37806c28).
