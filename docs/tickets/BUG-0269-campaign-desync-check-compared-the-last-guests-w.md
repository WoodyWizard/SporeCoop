---
id: BUG-0269
title: campaign: desync check compared the last guests with a host sample seconds old (false n / c desyncs of Carol and Dave)
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
no-fail-run: test tool change; desync findings of the 3rd and 4th player remain in lab 20261029-122937 (followed in BUG-0275).

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w6-campaign2, merged into main.
