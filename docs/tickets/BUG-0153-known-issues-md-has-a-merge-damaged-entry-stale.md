---
id: BUG-0153
title: known-issues.md has a merge-damaged entry, stale "fixed" and outdated entries, and no ticket ids
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#132
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: claude/clever-fermi-h57l81
commits: 940fc74, 7cbae8e
tests: make test (docs_lint, tools_selftest.py docslint)
signature:
docs: docs/known-issues.md
related: #132
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/132 (full text, discussion).

`docs/known-issues.md` merges with git's `union` driver, and one such merge damaged it. One entry is a bare heading with no body, which reads as an open bug that CHANGELOG 0.7.0 lists as fixed. The next entry lost its heading, so its first line is " of 20-27 m". An entry marked "fixed 2026-10-24" is still in the file, although the file header says fixed entries move out. Three more statements describe the code as it was before later merges. Finally, CLAUDE.md requires every entry to carry its ticket id, and the file has none.

### Where
- Empty entry: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L266-L268
- Lost heading: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L279-L289
- Fixed entry still listed: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L136-L157
- File header rule: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L1-L4
- Stale: Space trade rivals: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L411-L415
- Stale: F9 Space page rows: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L427-L429
- Stale: Space allies rivals: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L453-L454
- CHANGELOG 0.7.0 fix for the empty entry: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CHANGELOG.md#L244-L246
- The ticket-id rule: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L39-L40
- The union-merge rule ("After such a merge check the file for duplicated or interleaved text"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L127-L129

### How to reproduce
**How to reproduce / check**

Open the file at the lines above. Run `grep -c "BUG-" docs/known-issues.md`.

### Expected
- Every entry has a heading, a body and its ticket id.
- Fixed entries are gone (or marked as in CLAUDE.md "Corrections are added, not overwritten").
- Statements about branches and constants match main.

## Root cause
**What happens**

1. Line 266 is `## Space: \`coop.space jumpto\` into another system arrives without planets`, followed directly by the next heading. CHANGELOG 0.7.0 says: "The jump to a partner (and every co-op jump) into a star system nobody had visited arrived without planets and without first contact. It now travels like a player through the galaxy view". The file presents a fixed bug as open, with no text.
2. Line 281 is ` of 20-27 m`, the tail of a lost heading (the Civilization vehicle position outliers entry). Its body follows, so the entry has no title and no anchor. `git log -S"of 20-27 m"` points at the merge commit 6a7e123 (`tools/ghidra` into `feat/space-07`), and the damage was carried through 5e40ddf.
3. Line 136 is `## coop_sync.py: failures on GOG 2024 (lane 10) and turn lag (fixed 2026-10-24, fix/gog2024-sync)`, but the header says "Bugs found and not fixed yet ... Fixed entries move to the commit message (and a test)."
4. Stale statements, each re-checked against the code:
   - L411 "**Rivals mode is tested only in a local merge**: space_rivalry.cpp (feature 10) is not in this branch". space_rivalry.cpp is on main, and `coop_space_trade.py` runs its rivals part when `coop.space features` lists `rivalry:` (it does: space_rivalry.cpp registers `.name = "rivalry"`).
   - L427 "**The F9 Space page has 9 rows for every feature together** (coop_ui.cpp kSpaceRows): the trade rows ... can fall off the page". coop_ui.cpp:45 is `constexpr int kSpaceRows = 12;` with paging (`g_space_first`), as CHANGELOG 0.7.0 says ("pages with < and >").
   - L454 "not tested here (feat/space-rivalry not merged)". The branch is merged.
5. `grep -c "BUG-" docs/known-issues.md` gives 0, while CLAUDE.md says each entry has "its ticket id". Several entries have tickets (BUG-0019, BUG-0020, BUG-0021, BUG-0011, BUG-0013, BUG-0026).

### Confidence
Confirmed by reading. A second reviewer verified the damaged entries, the stale "fixed" entry and the missing ticket ids. The three stale statements were re-checked against main for this issue.

## Fix
Fixed 2026-10-27 (claude/clever-fermi-h57l81): the empty `coop.space jumpto` heading (fixed in 0.7.0) is removed, the lost heading is restored as "Civilization: single vehicle sync outliers of 20-27 m" (docs/stability/civ.md), the coop_sync entry is narrowed to its open leftovers, the stale statements are marked `~ superseded (2026-10-27)` instead of deleted, and every entry carries its ticket id (`* Ticket: BUG-NNNN`, 34 tickets backfilled: BUG-0167 to BUG-0200). The review suggested:

- Remove the empty jumpto heading (or give it a "fixed in 0.7.0" note), restore the Civilization heading with an anchor, and drop or move the coop_sync entry.
- Update the three statements.
- Add the ticket id to each entry, opening tickets for entries that have none (`tools/tickets.py new`).
- Have `tools/docs_lint.py` flag an `## ` heading followed directly by another heading, a body line starting with a lowercase fragment after a blank line at the top of an entry, and an entry without `BUG-NNNN`.

## Verification
`make test` (docs_lint, tickets.py check) passes; `grep -c "^\* Ticket: BUG-" docs/known-issues.md` is 40 entries + the template. Planned in the review:

`make test` (docs_lint) fails on the current file and passes after the cleanup.

### Related
#47 (another doc that drifted from the code).

Checked again 2026-10-28 (fix/w5-tools): the entries were repaired in 940fc74 (commits: was empty). Added the lint the issue asked for: docs_lint flags a known-issues entry with no body, a paragraph starting with a fragment and an entry without BUG-NNNN. FAIL: on 563f72d's known-issues.md it reports the empty jumpto entry (line 266), the lost heading " of 20-27 m" (line 281) and 38 entries without a ticket id. PASS: 0 problems on today's file; tools_selftest docslint 4/4. One more stale line corrected (g_host_away is reset since BUG-0085).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #132.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on claude/clever-fermi-h57l81 (ticket ids in every entry, merge damage repaired, stale statements marked superseded).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: docs_lint rules for known-issues.md added, commits filled in (fix/w5-tools, 7cbae8e).
