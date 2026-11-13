---
id: BUG-1013
title: Session report: the guest cuts the host's name to 40 bytes, a non-Latin host name ends in a broken character in the feed and the window
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-9
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: text_lines
signature:
docs: docs/known-issues.md#issue-sreport-host-cut, docs/06-testing.md#testing-p2-ui
related:
switch: core.sreport_host_cut
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-9. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
When the host asks for problem reports, a guest shows "<host> asks for your problem report" in the feed and the co-op
window with the host's name cut to 40 bytes. A host name of 14+ CJK characters (3 bytes each) or a mixed name over 40
bytes ends in a broken UTF-8 sequence there (a replacement box or garbage in the overlay).

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/sreport.cpp:543 and :557 `g_ask_host = a.host.substr(0, 40)`;
names are up to 24 code points (utf8.h:106, up to 96 bytes). The rest of the mod cuts with utf8::cut_bytes / head.

## Fix idea
`utf8::head(utf8::clean_text(a.host, ...), 24)` or cut_bytes(a.host, 40). No switch needed; `sreport.host_cut` if wanted.

## How to test
tests/text_tests.cpp cannot reach sreport.cpp's static; twoinst: coop_session_report.py with the host named with 14 CJK
characters (coop.name), assert the guest's window line (coop.sreport status / the ui text) is valid UTF-8 and ends
with the whole name: FAIL on main, PASS with the fix.

## Fix
sreport.cpp: the host name of an Ask goes through utf8::net_name (whole characters, 24 code points, clean UTF-8)
instead of substr(0, 40). Test seam `coop.sreport askname <name>|-` (the host name the next Asks carry). Switch
`coop.fix core.sreport_host_cut off`.

## Verification
coop_text_lines.py (suite `text_lines`): a host name of 14 CJK characters (42 bytes); the guest's guestline must
start with the whole name, no U+FFFD.
* FAIL (SPORE_FIX_OFF=...,core.sreport_host_cut,...): out/lab/20261113-032307 ('...練習<U+FFFD> (host) asks')
* PASS: out/lab/20261113-032115-2, out/lab/20261113-032426

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
