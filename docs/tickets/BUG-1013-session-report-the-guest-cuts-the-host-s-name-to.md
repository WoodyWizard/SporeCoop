---
id: BUG-1013
title: Session report: the guest cuts the host's name to 40 bytes, a non-Latin host name ends in a broken character in the feed and the window
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-9
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-sreport-host-cut
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
