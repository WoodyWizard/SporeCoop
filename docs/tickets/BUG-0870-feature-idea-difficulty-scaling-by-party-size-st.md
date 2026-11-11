---
id: BUG-0870
title: Feature idea: difficulty scaling by party size (stage progress, enemy health and damage, hunger, takeover, raids)
status: open
kind: feature
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages36/41-dev, BUG-9100 (founders.h CampaignRules)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
With four players the stages go by too fast and enemies are trivial. The fork scales the campaign by the number of
seats, with one slider (0..400 %).

## Root cause
Not a bug. We have `dna_pace` (single-player pace or every gain whole) only. Note: plan-0.7.12 F3 says "Not
proposed: anything that changes the game's own difficulty"; this needs the owner's decision.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages36/41-dev, BUG-9100, mods/coop/founders.h CampaignRules, world.cpp MSG_CAMPAIGN_RULES); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* Rules stored with the campaign (file FST7, 24 bytes; FST1..6 read as the old model) and sent in
  MSG_CAMPAIGN_RULES: seats (2..4), difficulty_percent, per-seat extras: enemy health +15 %, damage +5 %, hunger
  +2.5 % (basis points 250), city takeover +5 %, Space raid +1 raider, progress +60 % per extra seat (model 1).
* `extra_total(per_seat) = round((seats-1) * per_seat * difficulty / 100)`, rounded once over all seats; workload
  (stage progress, Cell food per DNA, rival targets, Civ military interval and fleet cap) = 100 + extra.
* Cell: from 3 seats one extra predator per native predator group, spawned at touching distance of the true
  collision sphere (model radius +0x1244 x scale +0x58) plus a gap, and plant food +25 % then (BUG-9109, 9110).
* Applies to new campaigns only; `coop.rules` prints the values. The fork's Civ / Tribe parts rest on its
  "reserved founders" (separate villages / nations), which we do not have.
* Overlap: plan-0.7.12 F3 (session settings) is the natural home; Rivals 0.8 needs its own balance.

## How to test
Lab, 2 instances: host a new campaign with 4 seats, 100 %: an NPC's max HP is 145 % of solo, its hit 115 %, the
Creature hunger drain 107.5 %; 0 % gives the solo values; an old save keeps its numbers.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
