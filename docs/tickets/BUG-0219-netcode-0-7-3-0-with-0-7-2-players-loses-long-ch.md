---
id: BUG-0219
title: Netcode: 0.7.3.0 with 0.7.2 players loses long chat lines and big species tables (or empties the table), and the "protocol did not change" note is wrong
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #153 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-net
commits: 57ca92f
tests: tests/net_tests.cpp test_old_peer_split, test_old_peer_session (net_tests --off peer_caps fails); tests/cell_tests.cpp meals_old_host (--old-trim fails); tests/fuzz_decoders.cpp hello_welcome_ext
signature:
docs: docs/04-netcode.md#netcode-hello-caps, CHANGELOG [0.7.3.0] correction, README
related:
---

## Report
From GitHub issue #153: https://github.com/WoodyWizard/SporeCoop/issues/153 (the full analysis, links and the scenario are there).

**Summary**
The 0.7.3.0 release notes say "The network protocol did not change: 0.7.3.0 plays together with 0.7.x". But 0.7.3.0 has a new wire feature. BUG-0034 (commit a8341cf) splits a reliable message bigger than one packet into pieces marked with flag bit 2, and a 0.7.2 receiver does not know that bit. It reads every piece as a whole message:
- a long chat line is lost;
- a host's species table with 42 to 83 species is read as an empty table (bigger tables are lost or, rarely, read as a few wrong records).

In the other direction, a 0.7.3 guest with a 0.7.2 host keeps every Cell meal it eats in a list that is never trimmed (BUG-0051 code).

Severity: minor desync in mixed-version sessions (no stall, no crash), plus a small memory growth per session, plus a wrong compatibility note.

**

## Root cause
Observed by reading and in the unit tests: BUG-0034 (a8341cf) split every reliable message above one packet (1163 bytes of payload) into pieces with wire flag 2 for any peer. A 0.7.2 receiver tests only flag 1 and delivers every piece as a whole message: a chat line above 1160 bytes fails to parse in each piece, a species table of 42-83 records reads as an empty table (the second piece starts inside a record's relation field). Protocol 7 was kept, so nothing told the peers apart. Separately, Meals::diff returned before its trim when the host sends no ack (0.7.2), so the guest's meal list grew by one per meal for the session.

## Fix
HELLO and WELCOME carry `u32 caps` after the 0.7.2 fields (readers of 0.7.2 ignore trailing bytes; protocol stays 7). Bit 0 CAP_SPLIT = "joins split reliable messages", bit 1 CAP_TOKEN = the rejoin token of BUG-0235 (HELLO only). `Connection::peer_split` (default false) is set from the peer's caps (host: HELLO; guest: WELCOME). `send_reliable` splits only for such a peer; for an older one a message above one packet is not sent and counted (`too_big_old`, a line "older peer N: too_big_old=..." after the session block of `coop.status`). Chat to an older peer (own and relayed lines) is cut between characters to `Connection::kMaxSingle - 3` bytes. The receiver drops pieces from a peer that never announced splitting (`frag_lost`). A notice names the older player once per join. `Meals::diff` clears the list when the host sends no ack. Switches: `coop.fix net.peer_caps off` (5801eea), `coop.fix pool.meals_trim off`. Note: a 0.7.3.0 peer has no caps either, so 0.7.3.0 with this version is treated like 0.7.2 (big messages not sent, chat cut): the safe side.

## Verification
`make test` (net_tests, ASan, fuzz_decoders): PASS. `net_tests --off peer_caps`: FAIL (the 0.7.2-style receiver sees 6 pieces, chat 6 and species 4 deliveries; the old guest gets 3000-byte lines and the species table). `cell_tests --old-trim`: FAIL (unacked=10000), with the fix 0. Lab regression on lane 6: see the report (chat_long, cell_net, nest_species, fix_registry). Not run against a real 0.7.2.4 game.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
