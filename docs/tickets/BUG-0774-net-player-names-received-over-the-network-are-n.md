---
id: BUG-0774
title: Net: player names received over the network are not sanitized (control characters, newline into logs and the slot marker)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0201, BUG-0087
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
<!-- not fixed yet -->

## Fix idea
Run `utf8::player_name()` on every received name: HELLO on the host (before the uniqueness loop) and every
`PlayerInfo.name` the client reads (WELCOME roster, PLAYER_JOIN); an empty result becomes "guest<id>". Switch
`net.name_clean`.

## Verification
<!-- not fixed yet -->

## How to test
Unit test in tests/validate_tests.cpp (Session with a fake transport): a HELLO with name "A\nB\x01" gives the
host's roster name "AB". Lab: tools/fakepeer.py joins with a name holding "\nplanet X"; the guest's received slot
marker has exactly one `planet` line. FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
