---
id: BUG-1126
title: A12: ticket stage field validated and normalised; known-issues.md pruned to open entries
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-hygiene
commits: 21bb1846, ef2d817c, a1208147, 1b9a79ba
tests: make test (tools_selftest tickets / tickets_check, tickets.py check, docs_lint)
signature:
docs: docs/known-issues.md, docs/archive/known-issues-2026.md
related: BUG-0153
---

## Report
A12 of docs/plan-0.7.15.md (KIT-11, KIT-10, CORE-12): the tickets' `stage` field had 38 spellings (All / all /
any, Civ / civ / Civilization, "Tribe/Civ", "none", "-", empty), so class statistics were unreliable and `check`
passed them; docs/known-issues.md ("bugs not fixed yet") had 249 entries of which 7 named an open ticket.

## Root cause
tickets.py validated status and kind but not stage; known-issues.md entries were never moved when their
tickets closed.

## Fix
* tools/tickets.py: STAGES (Cell, Creature, Tribe, Civ, Space, Adventure, Editor, Menu, Boot, UI, All, Installer,
  Tools; a list as "Creature, Tribe" in that order) with aliases (civilization, any, core, lab, tests, setup, SporeKit's
  game mode names, ...). `new` requires `--stage` and writes an alias as its stage, an unknown one is refused; `check`
  fails on any other spelling and names the one to write; `tickets.py stages [--fix]` lists the spellings and rewrites
  aliases (only the stage line). Tooling, no switch.
* ef2d817c: the stage line of 101 tickets: 94 by `stages --fix`, 7 by hand (BUG-0266..0269, BUG-0372: Tools;
  BUG-0784: Installer; BUG-0969: Tools). 38 spellings became 17 (13 stages and 4 lists). Private tickets were already
  valid (All / Space).
* known-issues.md: 12 entries stay (an open or game-bug ticket: BUG-0191, 0192, 0194, 0196, 0197, 0198, 0899 open;
  BUG-0304, 0313, 0371, 0436 game-bug); 237 entries whose tickets are all fixed / wontfix / not-reproduced / duplicate
  moved unchanged with their anchors to docs/archive/known-issues-2026.md (union merge); 110 references to their
  anchors and the relative links inside them adjusted; docs_lint clean.

## Verification
* FAIL: tools_selftest tickets_check with `stage: civ`, `Tribe/Civ`, `Moon` and an empty stage: check exits 1 with
  "is written" / "is not one of" (selftest cases, all pass as expected failures); before this change `check` passed
  all 38 spellings (tickets: 1110 ticket(s), 0 problem(s)).
* PASS: `tools/tickets.py check` on the normalised tickets: 0 problems; `new --stage civilization/tribe` writes
  "Tribe, Civ", `new --stage Moon` and `new` without a stage are refused (selftest).
* no-fail-run: the known-issues move is docs only (docs_lint 0 problems).

## History
- 2026-11-15: opened.
- 2026-11-15: done on feat/w51-hygiene (21bb1846, ef2d817c, a1208147, 1b9a79ba).
- 2026-11-18: released in 0.7.15.
