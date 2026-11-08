---
id: BUG-0692
title: Shared money / food: a guest's spend right after an editor visit is undone by the next host total (the sync state dropped on the mode switch)
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state-b
commits: f1d45292
tests: res_away_tribe
signature:
docs: docs/re/host-centric.md#host-centric-away-credits
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. shared.cpp tick sets have=false whenever the game mode is not the resource's (an editor visit: kEdit / kEditor). Back in the stage, the guest waits for the host's next total with have=false, and a spend made in that window (up to 2 s) is not folded: the host's total overwrites it on the guest's screen (the purchase made, the money back: spent twice) and the host never counts it.

## Root cause
Observed (lab, guest log `sync state dropped out of its mode`): the guest's tick set have=false in every frame out of the resource's mode; a purchase in the first frame back was not folded and the host's next total undid it (Tribe food 500 -> 300 -> 500). Civ money is not hit in practice: the host's income changes it every frame, its total comes every frame.

## Fix
`res.mode_keep`: out of the mode the guest keeps its sync state while game::stage_epoch(mode) is the same (an editor visit keeps it, a load raises it).

## Verification
* FAIL with `SPORE_FIX_OFF=res.mode_keep`: out/lab/20261107-223832, out/lab/20261107-223949 (res_away_tribe).
* PASS: out/lab/20261107-224108 (res_away_tribe), out/lab/20261107-223030.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
- 2026-11-08: released in 0.7.11.
