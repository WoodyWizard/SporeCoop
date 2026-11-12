---
id: BUG-0767
title: One report for the session: the host asks every guest for its report, one session zip
status: fixed
kind: feature
stage: All
reported: 2026-11-08
source: Discord suggestions 2026-10-31..11-04 (Comfortable bug reports), docs/plan-0.7.12.md F4
role: all
versions: 0.7.11
fixed_in: 0.7.12
branch: feat/w46-session-report
commits: 04061890, c941e143, 2eb251bc
tests: session_report, unit (validate_tests test_sreport, fuzz_decoders fuzz_sreport)
signature:
docs: docs/REPORTING.md#reporting-session, docs/08-ui.md#ui-session-report, docs/06-testing.md#testing-session-report
related: BUG-0765, docs/plan-0.7.12.md F4
---

## Report
Discord suggestion "Comfortable bug reports" (forum "suggestions", 2026-10-31..11-04): the host's report should hold
every player's report. Plan 0.7.12 F4 (approved by the owner 2026-11-08, plus a host button "Ask the players for their
reports" usable at any time without the host's own report).

## Root cause
Not a bug: until 0.7.11 every player made its own SporeCoop-report zip and had to send it; a session's problem needed
the files of every player and usually one was missing.

## Fix
`mods/coop/net/sreport.h` (portable: MSG_REPORT kinds, `check_zip` / `allowed_name`, the streaming `ZipOut`, the host's
`Round`) and `mods/coop/sreport.cpp` (the game side). MSG_REPORT = 68, class Session in host_world.h. The host's
"Report a problem" (window and pause menu) and "Ask the players for their reports" (`ask_players_for_reports`, the F1
stub) send a question to every guest; the guest's window opens on the lobby with "<host> (host) asks for your report:
Send / Don't send" and what is sent; Send makes the guest's own report (`make_report`, with its screenshot) and sends it
in 1000-byte reliable pieces; the host refuses a report over 25 MB at the announcement, stops one that sends more than it
announced, checks the CRC of the transfer and the zip (structure, only the names of a SporeKit report archive, CRC of
stored entries), never unpacks it, and writes `SporeCoop-session-report-<time>.zip` (Host-<name>.zip, Guest-<name>.zip,
SESSION.txt; via a .part file renamed when complete). Guests that do not answer in 120 s or stall 60 s are "timed out",
a guest that leaves is "left". The plan's "no folders" is read as "only the six folders a SporeKit report has" (logs/,
previous/, crashes/, reports/, shots/, settings/; one level). Twin switch `coop.fix core.session_report off`: nobody is
asked (0.7.11). Console `coop.sreport` (status, ask, answer, test hooks big / overrun / badname / junk).

## Verification
`coop_session_report.py` (suite `session_report`, four players, real clicks): PASS out/lab/20261108-163527,
out/lab/20261108-163752 and out/lab/20261108-164730 (lane 4, the final code 2eb251bc, together with report_shot, ui,
ui_explain on lanes 4-5: all OK); FAIL with `SPORE_FIX_OFF=core.session_report` out/lab/20261108-163757 (lane 5, c941e143:
no guest is asked in rounds A, B, C; the host's report alone still works). report_shot failed once in
out/lab/20261108-164357 (the round's line had replaced the host's "Report saved ... screenshot" status; fixed in 2eb251bc
with the round's count in its own label). Unit: validate_tests `test_sreport`
(names, the zip check incl. every prefix of a valid zip under ASan, a round with sent / not sent / too big / overrun /
junk / out of order / timed out / left), fuzz_decoders `fuzz_sreport` (6000 mutated archives through the check and a
round). `make test` passes.

## History
- 2026-11-08: opened; implemented on feat/w46-session-report (F4 of 0.7.12).
- 2026-11-11: released in 0.7.12.
