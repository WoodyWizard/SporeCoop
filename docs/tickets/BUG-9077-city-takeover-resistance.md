---
id: BUG-9077
title: Reserved-founder city takeover resistance across all three routes
status: fixed
kind: feature
stage: Civilization
reported: 2026-11-06
source: Approved five percent per extra reserved founder
role: host guest
versions: duo20-dev
fixed_in: unreleased
branch: feat/lineages-civ-combat
commits: 3ad8b6de
tests: tools/tests/duo_civ_takeover.py
signature: Military religious and economic takeover effort lacks reserved-seat resistance
docs: docs/re/civ.md
related: BUG-9076
---

## Scope

Add five percent takeover effort per extra reserved founder to military
capture, religious conversion and economic trade eligibility. Keep combat
health/damage, native purchase prices and trade income unchanged. Retain
fractional converted citizens through native saves and campaign transfers.

## Restart regression

lineages-takeover-a.log passed native military/economic contribution checks
and saved a withheld citizen fraction. lineages-takeover-cold-a.log then
failed: the host's join-time native save changed the planet digest before
the first preaching action loaded the attachment. Load fractional credit
before every host native save, then publish it against the new checkpoint.

## Verification

lineages-takeover-a.log compares the switch off/on on two local games:
military progress 10 -> 8.695652 and economic progress 11.428572 -> 9.937887,
with a synchronized military meter. One raw citizen retains its fraction;
the next makes native conversion progress. Founder-targeted trade progress
remains unchanged. Native follower-map counts are inspected directly.

FAIL: lineages-takeover-cold-a.log lost the saved fraction when the guest
joined. The source attachment matched its native planet's SHA-256; the
copy's join-time save changed the planet without rebinding the attachment.
PASS: lineages-takeover-cold-b.log restores that fraction after restart and
guest transfer, then completes a religious takeover on both native cities.
PASS: lineages-takeover-cold-c.log repeats the same regression and takeover
using the installed, stripped Duo 21 DLL.

The installed, stripped Duo 21 package passes lineages-takeover-b.log
(military capture completion) and lineages-takeover-c.log (economic
eligibility, the unchanged 2,500 shared debit and ownership on both peers).
Both repeat the contribution, teammate exemption and fractional tests.
All completed native runs report zero faults and clean quit acknowledgments.

Windows unit tests pass: one/two/four reserved seats, FST5 migration,
custom coefficients, truncated/overlong rule files, small integer contributions,
large counts, persistence, duplicate records and stale checkpoint rejection.
The actual ZIP install/uninstall/reinstall fixture preserves save/settings
sentinels and restores the original loader (lineages-duo21-package.log).

These direct native calls qualify contribution and ownership paths. Complete
mouse-driven religious/economic play, guest conversion/trade meter rendering,
four active players and two-PC networking remain separate qualification work.
