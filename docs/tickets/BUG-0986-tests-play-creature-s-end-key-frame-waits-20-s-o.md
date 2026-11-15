---
id: BUG-0986
title: tests: play_creature's end key frame waits 20 s on both games in the creature editor after an evolution vote of the bot
status: fixed
kind: test
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 (key-frame review)
role: 
versions: 0.7.13-rc
fixed_in: 0.7.13
branch: fix/w48-fui
commits: f4fa2eb9
tests: play_creature
signature:
docs:
related:
---

## Report
Key-frame review of the rel/0.7.13 final check (lab out/lab/20261112-175447, sheet
043_play_creature_03_end.jpg): both games in the creature editor at the end frame ("busy editor", waited 20.2 s),
while the note expects both games in Creature play.

## Root cause
Observed (host log): at 19:29:57 the host's bot at the nest with 92 DNA started the evolution
(`vote: start 'Alice wants to evolve (Creature)'`), at 19:30:56 `vote: end: time is up, ready 1/2`, both games went
into the editor together, the play ended 1 s later. That is the game and the mod working as they should (the
evolution vote); the frame's note did not allow it. Test artefact.

## Fix
tools/tests/play_creature.py: when both games are in an editor at the end, the end frame says so (the play
ended in an evolution: both editors, each with its own creature) and is shot at once (no 20 s settle).

## Verification
no-fail-run: a key frame's note and timing only (the frame never fails the test); the case needs a 10 min play
that ends in a vote.

## History
- 2026-11-12: opened.
- 2026-11-12: test note fixed; fix/w48-fui.
- 2026-11-12: released in 0.7.13.
