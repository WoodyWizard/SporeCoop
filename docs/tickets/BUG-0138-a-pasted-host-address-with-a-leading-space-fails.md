---
id: BUG-0138
title: a pasted host address with a leading space fails as "cannot resolve", and a wrong port is silently accepted
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#117
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: aa6d418, 1e905bb, 423392b
tests: coop_ui_status.py (suite ui_status), net_tests test_address_parse
signature:
docs: docs/06-testing.md#testing-net-w3
related: #117, BUG-0204
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/117 (full text, discussion).

The Join field and `Endpoint::parse` accept or reject addresses in ways the player cannot understand:

- Only trailing spaces are trimmed. A leading space, which is typical when copying from a chat ("Friends connect to: 192.168.1.5:47810"), gives "Cannot join: cannot resolve  192.168.1.5:47810". So does pasting the whole hint line with two addresses.
- The port is read with `atoi` and cast to `uint16_t`. "1.2.3.4:" becomes port 0, "1.2.3.4:abc" becomes 0, and "1.2.3.4:99999" becomes 34463. No error is shown; the player waits 10 s for "no answer from host (timeout)".
- Host names are resolved with a blocking `getaddrinfo` on the game's main thread, inside the click handler. With slow or unreachable DNS, the game freezes for that time.

### Where
- Trimming only at the end: [coop_ui.cpp#L288-L306](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L288-L306)
- Parsing and resolving: [net/udp.cpp#L54-L74](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/udp.cpp#L54-L74)
- Called from `Session::join`: [net/session.cpp#L47-L55](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L47-L55)

### How to reproduce
- Paste `" 192.168.1.5:47810"` (leading space) into "Host address" and press Join: "Cannot join: cannot resolve ...".
- Type `192.168.1.5:99999` and press Join: "Connecting to 192.168.1.5:99999..." then a timeout after 10 s. The log shows `joining 192.168.1.5:34463`.
- Type a host name whose DNS server does not answer: the game stops drawing until `getaddrinfo` returns.

### Expected
- Whitespace (spaces, tabs, newlines) trimmed on both sides.
- A port outside 1..65535, or not a number, rejected with "the port must be a number from 1 to 65535".
- DNS resolution off the main thread (or numeric addresses only, with a clear message).

## Root cause
**What happens**

A host-side copy of `Endpoint::parse` (numeric addresses):

| Input | Result |
|---|---|
| `" 192.168.1.5:47810"` | fails, "cannot resolve" |
| `"192.168.1.5:47810"` | ok, port 47810 |
| `"192.168.1.5:"` | ok, **port 0** |
| `"192.168.1.5:99999"` | ok, **port 34463** |
| `"192.168.1.5:47810,  10.0.0.2:47810"` (the hint line) | fails, "cannot resolve" |

With a port of 0 or a wrapped port, the guest sends to the wrong port. The join then ends with "no answer from host (timeout)", which points the player to the host's firewall instead of their typo.

### Confidence
CONFIRMED, low severity: a host-side harness ran on the real `Endpoint::parse` logic. A second reviewer verified it.

## Fix
`Endpoint::parse(s, port, out, err)` (net/udp.cpp) trims whitespace on both sides and takes the
first address of a pasted list (`Endpoint::clean`: up to a space, `,` or `;`); the port is read
with `strtoul` and must be 1..65535 ("the port must be a number from 1 to 65535"); an empty host
is refused. `Session::join` passes the reason through, so the window says "Cannot join: the port
must be ...". A numeric address is parsed with `AI_NUMERICHOST` first and never waits for DNS.
The co-op window's `do_join` cleans the text the same way (and saves the cleaned address).
Switch: `coop.corefix addr_check 0` / `net::g_fix.addr_check`.
Not fixed here: a host *name* still resolves on the main thread (a slow DNS server freezes the
game): split into BUG-0204.

## Verification
Unit test `test_address_parse` (FAILS with `net_tests --nofix`: " 192.168.1.5:47810" gives
"cannot resolve"; PASS with the fix). In game `coop_ui_status.py` part 1 types `127.0.0.1:<port+60000>`
then ` 127.0.0.1:<port>` into the real address field and presses Join.
- FAIL: `coop_ui_status.py --without-fix` on lane 2 (switches off), logs `out/lab/manual-w3-net/ui_status_nofix.log` and `ui_status_nofix2.log` (2026-10-28): "Connecting to 127.0.0.1:107812..." (the wrong port accepted), then
  "Cannot join: cannot resolve  127.0.0.1:47812".
- PASS: lab runs `out/lab/20261028-005022` and `out/lab/20261028-011423` (lane 2, both PASS): "Cannot join: the port must be a number from 1 to 65535", then joined.

### Related
#8 (join timeout), #83 (the status keeps "Connecting to ..." after such a failed join).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #117.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
