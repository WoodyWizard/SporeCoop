---
id: BUG-9244
title: Creature: a species' goal card says it is extinct while its creatures still live (the line shown twice)
status: open
kind: bug
stage: creature
reported: 2026-11-16
source: owner 2026-11-16 (62-dev QA)
role: 
versions: 0.7.14.1-lineages62-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Owner 2026-11-16 (62-dev, a QA game on TITAN, screenshot): the Hunt card says "The Crikiitis species is extinct!" twice while Crikiitis stand next to the player. Seen on the QA guest Titan too ("The Bye Mouth species is extinct!", out/shots/r66/fill_2.png).

## Root cause
Not looked into yet. To check: the species' extinct flag / goal outcome on the host vs the guest (social.cpp follows the host's table; BUG-0460 extinct nests), and whether the card line is added twice.

## Fix

## Verification

## History
- 2026-11-16: opened from the owner's screenshot.
