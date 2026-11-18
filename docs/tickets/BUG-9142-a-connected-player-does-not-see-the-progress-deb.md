---
id: BUG-9142
title: a connected player does not see the progress debt on the progress bar
status: open
kind: bug
stage: Creature Cell
reported: 2026-11-12
source: owner report 2026-11-12 (a connected player)
role: guest
versions: 0.7.8-lineages53-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: QA 2026-11-12 (the MSI as guest)
signature:
docs: docs/tickets/BUG-9119-progress-debt.md
related: BUG-9119
---

## Report
Owner: "A connected player doesn't see the Debt in the progress bar." The owner hosts; the friends are the connected players (logs of the owner's sessions show debts in Cell and Creature).

## Root cause
Not reproduced. On two PCs (the MSI as guest, Creature) the red stretch shows: `coop.debt add 30` and a real knockout (`coop.giveup down`, the 30 s ran out, 20 DNA debt): bar found, tinted by the shader, at the arrow. A debt of 20 on the 1000-point bar is about 20 px: easy to miss. Inferred candidates for a friend's game: the HUD hidden (death screen) until the debt was repaid (Cell repays in seconds), a short debt, or a draw refusal not logged.

## Fix
Diagnostics: progress_debt logs once per debt when nothing was drawn for 5 s, with the reason (bar not on screen, no progress value, empty fill, no device) and the bar numbers: `coop: progress debt N (guest) not drawn for 5 s: ...`.

## Verification
no-fail-run: not reproduced; the next report's log names the cause.

## History
- 2026-11-12: opened.
