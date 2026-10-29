---
id: BUG-0268
title: campaign: dialogs a player answers (a nation's 'Agreed. Bye for now.', a badge's Reward panel) block civ -> space and the finale
status: fixed
kind: tooling
stage: 
reported: 2026-10-29
source: lab 20261029-093233 campaign
role: 
versions: 
fixed_in: unreleased
branch: fix/w6-campaign2
commits: f617994
tests: campaign
signature:
docs:
related:
---

## Report
Lab 20261029-093233 campaign runs (see the title and the fix commit).

## Root cause
The bot did not handle these (see the title).

## Fix
See the commit message.
switch: none (test tooling)

## Verification
no-fail-run: test tool change; failing run lab 20261029-093233 campaign#2; in lab 20261029-122937 every civ -> space change finished (3 of 3) and two of three finales passed (BUG-0277 is the third).

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w6-campaign2, merged into main.
