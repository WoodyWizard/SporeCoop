---
id: BUG-9228
title: Editor: the Party cards cover the paint page's next-page button
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15
role: all
versions: 0.7.14.1-lineages58-dev
fixed_in: 0.7.14.1-lineages59-dev
branch: feat/lineages-47
commits: 95378ac3
tests: none yet (built; the owner's session was running)
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "Party is covering the next page button in the editor for painting."

## Root cause
Observed in the code: in an editor the cards sit low at the right edge (ui.party_editor_spot, BUG-9191), where the paint page's next-page button is.

## Fix
- The Party cards are hidden while an editor is open. Switch ui.party_editor_hide.

## Verification
no-fail-run: built only; to be checked on TITAN + the MSI after the owner's play session

## History
- 2026-11-15: opened.
