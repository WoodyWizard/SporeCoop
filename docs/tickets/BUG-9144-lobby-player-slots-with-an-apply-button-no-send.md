---
id: BUG-9144
title: lobby: Player Slots with an Apply button; no Send World button
status: fixed
kind: feature
stage: all
reported: 2026-11-12
source: owner request 2026-11-12
role: host
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: c1ec1834
tests: tools/tests/coop_ui_explain.py (updated); the renamed status in the duo_* lab tests; QA 2026-11-12
signature:
docs:
related: BUG-9135 BUG-0597 BUG-9111
---

## Report
Owner: "Seats Now sounds corny. Maybe we add an apply button?"; "Instead of saying seats, we should say Slots or Player Slots"; "Send world ... shouldn't really exist. That stuff should be automatic."

## Root cause
Requests. The world already goes to every player by itself (on a join, a load, a stage change, a guest in another mode for 5 s); the button was a manual fallback that doubled as the vote's "Start Now".

## Fix
Lobby: "Player Slots [n] [Apply]" (the box shows the slots in use; more apply at once, fewer from the next stage; Space: next load); the host page "Player Slots"; an empty row "Open Slot"; every player-facing text says player slots (status "Player slots: 4, difficulty ...", "Host campaign rules: 4 player slots."). The Send World button is gone; the same button is the host's "Start Now" only while a ready vote runs; Player Cells and Foreign Creations move up. `coop.share` stays for support.

## Verification
no-fail-run: wording and layout. QA 2026-11-12 host screenshot: "Player Slots 4 Apply", no Send World. The lab tests' expected strings were updated (not run: Linux lab).

## History
- 2026-11-12: opened.
