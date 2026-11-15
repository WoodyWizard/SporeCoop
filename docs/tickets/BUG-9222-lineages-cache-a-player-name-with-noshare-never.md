---
id: BUG-9222
title: Lineages Cache: a player name with -NoShare- never shares or stores its creations (QA players)
status: fixed
kind: feature
stage: all
reported: 2026-11-15
source: owner 2026-11-15
role: all
versions: 0.7.14.1-lineages56-dev
fixed_in: 0.7.14.1-lineages57-dev
branch: feat/lineages-47
commits: 79b4806b
tests: manual Windows QA: the MSI, two games, tests/text_tests.cpp
signature:
docs:
related:
---

## Report
The owner (2026-11-15): "add a key code to a Username so that it doesn't save to other player's Lineages Cache. -NoShare-. Use this every time you connect a test / QA version to my install. I'm currently having to delete all the weird looking creatures that break immersion." Usage: the usual name with -NoShare- added ("MSI-NoShare-").

## Root cause
Not a bug: every player's saved creations were offered to every partner (BUG-9112 rules) and kept in their Lineages Cache.

## Fix
- utf8::no_share_name: "-NoShare-" in any case anywhere in the name. The sender offers none of its own creations; a receiver keeps no creation whose direct sender or recorded sharer has the tag (both sides, so an older QA build cannot slip one into a newer owner build). Switch library.no_share; counter no_share_dropped in coop.library status.
- The tag is not shown in the player list or the joined / left lines (utf8::shown_name: "MSI-NoShare-" shows as "MSI").
- A name over 24 letters keeps its tag: the name before it is cut (utf8::player_name). A name typed with a space in a console command ends at the space (coop.join takes one word).
- QA scripts and tools/qa_win/README.md use the tagged names.

## Verification
PASS (unit, tests/text_tests.cpp): no_share_name and shown_name cases.
no-fail-run: the live exchange was not seen on the MSI (two games, MSICivHost and MSIGuest: the guest had no new creation to offer, nothing was exchanged either way); to be checked in the next session with a new creation.

## History
- 2026-11-15: opened.
- 2026-11-15: done.
