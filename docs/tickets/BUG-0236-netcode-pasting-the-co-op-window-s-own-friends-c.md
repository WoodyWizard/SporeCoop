---
id: BUG-0236
title: Netcode: pasting the co-op window's own "Friends connect to: ..." line into the Join field fails with "cannot resolve Friends"
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #170 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-net
commits: 57ca92f
tests: tests/net_tests.cpp test_address_label (net_tests --off addr_label fails); test_address_parse unchanged
signature:
docs:
switch: net.addr_label
related:
---

## Report
From GitHub issue #170: https://github.com/WoodyWizard/SporeCoop/issues/170 (the full analysis, links and the scenario are there).

**Summary**
BUG-0138 (commit aa6d418, release 0.7.3.0) added `Endpoint::clean` so that a host address copied from a chat works. Its comment, and the Join button's comment, say this includes "the whole pasted hint line". But the hint line the host's co-op window shows starts with a label: `Friends connect to: 192.168.1.5:47810,  10.0.0.2:47810`. `clean()` keeps the first word, "Friends", which is then looked up as a host name; the join ends with "Disconnected: cannot resolve Friends". Severity: minor (the player has to delete the label by hand). This is an incomplete fix: the case the report named is not covered.

**Where**
- `Endpoint::clean`: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/mods/coop/net/udp.cpp#L63-L74
- `parse_numeric` runs `clean()

## Root cause
Observed in the unit test: Endpoint::clean took the first word, "Friends" of the hint line "Friends connect to: a:47810,  b:47810", which then went to the name lookup ("cannot resolve Friends").

## Fix
`Endpoint::clean` splits at whitespace, ',' and ';' and returns the first token of the form `<host>:<1-5 digits>`, else the first token as before (so "192.168.1.5:", ":47810", "...:abc" keep their port errors). Switch `coop.fix net.addr_label off` (also inside `net.addr_check`).

## Verification
`net_tests`: PASS (English and Russian hint lines, a chat prefix, text after the address, "myhost.lan" still a name). `net_tests --off addr_label`: FAIL (4 cases).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
