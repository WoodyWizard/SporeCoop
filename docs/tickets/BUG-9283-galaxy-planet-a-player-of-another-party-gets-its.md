---
id: BUG-9283
title: Galaxy planet: a player of another party gets its own free nest spot, away from other players' nests
status: fixed
kind: feature
stage: creature
reported: 2026-11-19
source: owner 2026-11-19 (7E A)
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits:
tests: tools/qa_win (live TITAN QA 8 + QA 7 with a fresh profile, 2026-11-19)
signature:
docs: docs/ROADMAP.md (7E)
related:
---

## Report
Decision 7E A (owner 2026-11-19): on a shared planet a new player of another party gets a free spot of its own, away
from other players' nests; its party's members join it there. Found in BUG-9280: every player's nest was put 45-80 m
from the host's (decision 2, the co-op party rule), so strangers lived in the host's yard and their villages followed.

## Root cause
home_nests.cpp placed and looked for every seat's nest within 160 m of the host's home.

## Fix
home_nests.cpp (switch `creature.stranger_nest_apart`): a seat whose player is in another party than the host's
(parties::table_key) on a galaxy planet has its nest looked for on the whole planet; a new one goes 45-80 m from a
nest of its own party's members if one is here, else site_apart: rings of 300-650 m round the host's home, at least
250 m from every player's nest, then the usual flat, dry spot. The share hold (BUG-9210) uses the same range.
Also: the sync check no longer compares the evolution bar on a galaxy planet (each party has its own, BUG-9277; it
reported "dna: host 1078, guest 0" between two parties).

## Verification
no-fail-run: a feature. TITAN 2026-11-19: QA 7 with a profile never on Samuth joined QA 8's Samuth (Creature) as
another party: "a spot of its own 295 m from the host's home, 220 m or more from every player's nest (7E A)", its
nest made there with 3 members. Not seen yet: a second member of that party getting its nest by the first one's; a
player whose old co-op seat already has a nest by the host's keeps it (QA 9's old seat did: an existing nest stays).

## History
- 2026-11-19: built on feat/lineages-alpha.
