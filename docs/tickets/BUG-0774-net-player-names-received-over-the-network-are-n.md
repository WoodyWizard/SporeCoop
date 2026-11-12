---
id: BUG-0774
title: Net: player names received over the network are not sanitized (control characters, newline into logs and the slot marker)
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all
fixed_in: unreleased
branch: fix/w48-net
commits: fa0b0a65, 2f9c631b, 5857b6d1
tests: net_names, unit (make test: net_tests test_name_clean)
signature:
docs: docs/04-netcode.md#netcode-network-input, docs/06-testing.md#testing-n1-net
related: BUG-0201, BUG-0087
switch: net.name_clean
---

## Report
Found by reading the code. Names that come over the network (a guest's HELLO name on the host, the roster names and
the host's own name on the guests) are used as they arrive: up to 255 bytes, with control characters, newlines or
broken UTF-8. A modified client or host can put fake lines into the other players' logs (and the problem reports
built from them), oversized or garbled names into the co-op window and the feed, and, as the host, change the
marker file of the co-op save slot that its guests write.

## Root cause
Kind bug, not security: tools/tickets.py keeps every open security ticket private, and this one is judged not exploitable against players in practice (see below); it is a hardening item.
Inferred from the code (main e49e1e23), confidence medium (the effects on the game's UI text are not checked):
* mods/coop/net/session.cpp:494 `p.name = h.name.empty() ? ... : h.name` (raw `str8`); session.cpp:620 / 658 the
  client keeps the WELCOME / PLAYER_JOIN roster names raw.
* `utf8::player_name()` (net/utf8.h:107, drops < 0x20 and 0x7F, cuts to kNameMax) is applied only to the local name
  (coop.cpp 4645, 4664, 4763, 5586, 5971).
* slotfs.cpp `write_locked` writes `"host " + host + "
planet " + planet + "
"` into sporecoop.txt, and
  `pick()` / `tidy()` match slots by `marker_field(m, "host")` and `"planet"` (first line with that key): a host name
  holding `"
planet X"` changes which co-op slot is reused or moved to the backup folder.
Not judged exploitable against players in practice (needs a modified peer the victim chose to play with; no code
execution, no file outside the Games folder), so not private.

## Fix
Confirmed on the code (rel/0.7.13 b4ad58b5): session.cpp kept the HELLO name, the WELCOME roster and PLAYER_JOIN
names as received; the lab FAIL run shows a host and a guest log line made by a name ("planet X" on a line of its
own) and the host's roster row cut at the newline.

`utf8::clean_text` / `utf8::net_name` (net/utf8.h): one line of valid UTF-8, no C0 / C1 controls, DEL, U+2028 / 2029,
broken or overlong sequences or surrogates; names at most 24 code points, empty becomes "guest<id>" / "host".
Applied on every name from another machine: the host's HELLO (before the seen / rejoin / unique-name rules), the
guest's WELCOME roster and host name, PLAYER_JOIN, version texts (120), the LAN list's DiscoverReply name and info
(64), the WorldOffer planet name (it goes into the slot marker sporecoop.txt) and the log line of a refused file
name. Switch `coop.fix net.name_clean off`. Compatible with 0.7.12: its local names are already clean.

## Fix idea
Run `utf8::player_name()` on every received name: HELLO on the host (before the uniqueness loop) and every
`PlayerInfo.name` the client reads (WELCOME roster, PLAYER_JOIN); an empty result becomes "guest<id>". Switch
`net.name_clean`.

## Verification
* Lab `net_names` (coop_net_names.py, a scripted guest whose HELLO name holds "\nplanet X\r\n[12:00:00] FAKE
  LINE", control and broken bytes and 60 letters): FAIL with SPORE_FIX_OFF=net.name_clean, out/lab/20261112-111316-2
  (host row "[2] Evil", fake lines "planet X" in the host's and Bob's logs); PASS out/lab/20261112-111428 and
  out/lab/20261112-111524.
* Unit (make test): net_tests `test_name_clean` (HELLO, WELCOME, PLAYER_JOIN, LAN list, the clean_text rules):
  FAIL `net_tests --off name_clean` (8 failed checks, out/n1-unit/net_off_name_clean.txt), PASS in make test twice
  (out/n1-unit/net_pass_1.txt, net_pass_2.txt).
* The slot marker part (a host name with "\nplanet X"): the guest's roster name is cleaned (unit case "Host\nplanet
  Evil" -> "Hostplanet Evil"), the planet name of the offer too; a lab host cannot send such a name (its own name
  is cleaned locally).

## How to test
Unit test in tests/validate_tests.cpp (Session with a fake transport): a HELLO with name "A\nB\x01" gives the
host's roster name "AB". Lab: tools/fakepeer.py joins with a name holding "\nplanet X"; the guest's received slot
marker has exactly one `planet` line. FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-net (0.7.13 N1): names cleaned on receipt; FAIL/PASS lab and unit runs.
