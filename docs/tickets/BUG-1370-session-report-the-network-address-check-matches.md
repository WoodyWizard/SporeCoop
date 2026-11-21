---
id: BUG-1370
title: session_report: the network address check matches the four-part version 0.7.15.5 in SESSION.txt
status: open
kind: test
stage: UI
reported: 2026-11-20
source: lab 20261120-133703
role: host
versions: rel/0.7.16 673f7a55 (coop 0.7.15.5)
fixed_in:
branch: fix/w61-multi
commits:
tests: session_report
signature:
docs: docs/06-testing.md
related: BUG-1137, BUG-0767
---

## Report
Nightly of the second 0.7.16 final check (lab 20261120-133703, lane 4): `session_report` failed exactly three checks,
`[A] / [B] / [C] SESSION.txt has no network address`; every other check of the four rounds passed (each guest's
answer, the refusals, the zips byte for byte, no fault). It did not run in the first final check (20261120-041304);
its last runs (rel/0.7.14, 20261114-050940 and before) passed.

## Root cause
Test. Observed: the three session zips of that run (lane 4 `mods/logs/SporeCoop-session-report-20261120-2038*.zip`)
hold no address; the only dotted quads in SESSION.txt are the versions of the B6 lines (BUG-1137, added in 0.7.15):
`this game coop 0.7.15.5, SporeKit 0.3.18, GOG 2024 dc04aee5; wire ...` and one `peer N coop 0.7.15.5` line per guest.
The check's regex `\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}` matches `0.7.15.5`. The B6 lines came with 0.7.15 (three
parts) and the test was not run on a four-part version until now (0.7.15.5 on rel/0.7.16), so the mod is right and
the check is wrong; a 0.7.16.1 would fail it the same way.

## Fix
coop_session_report.py `addresses()`: the version right after `coop ` / `SporeKit ` is taken out before the search
for dotted quads; any other dotted quad (an address anywhere else in the file) still fails, and the check now prints
what it found. Two asserts at the top of the script pin both sides (versions are not found, `127.0.0.1` and
`192.168.1.20` next to a version are). Not loosened: the address search is the same, only the version token is skipped.

## Verification
Open, waiting for the lab run.
* FAIL (the old check): out/lab/20261120-133703 session_report (3 FAIL lines, on SESSION.txt with versions only).
* The checker on the three real SESSION.txt files of that run: old regex finds `0.7.15.5` x4 in each, the new one none.
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run session_report --lane N --retry 0 --no-build`;
  the twin `SPORE_FIX_OFF=core.session_report` must still FAIL (no guest asked, no session zip).

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-multi): the cause is the four-part version in the B6 lines; the check skips the version token.
