---
id: BUG-1420
title: A guest whose game stands in its own editor is dropped silently, or kept for good through the keepalive: kick it after 30 s, told, so it can join again
status: fixed
kind: bug
stage: All
reported: 2026-11-22
source: PR #216 review (reverse case)
role: host
versions: 0.7.16.1
fixed_in: unreleased
branch: fix/w63-editor
commits: 35163c73, 2a454d1e, 2bb64f64, 1718b4c2
tests: editor_stand_guest, twins editor_stand_vote_off, editor_stand_told_off; unit test_timeout_told (net_tests)
signature: host: "player N (...) disconnected (reason 1)" while the guest loads its own editor; the guest plays on a dead link
docs: docs/04-netcode.md#netcode-editor-long-wait
related: BUG-1419, BUG-1239, BUG-1369, BUG-0687, BUG-1108
---

## Report
The reverse case of PR #216 (BUG-1419), the owner's rule for 0.7.16.1: a guest stuck in its own editor (0.7.16 opens a
guest's own City Hall / vehicle editors at its Civ arrival, its own spaceship editor, its own Creature editor) must
not hold the host: after 30 s it is removed from the session and may join again. Found by reading the code (review
of PR #216, out/research/prs-213-220.md): no run.

## Root cause
Inferred from the code. How the host waits for a guest today:
* The session: one `timeout_ms` (30 s) for all peers (net/session.cpp, the host's timeout loop). A guest whose game
  stands without pumping messages (an editor's load) is dropped after 30 s, as the owner wants, but silently: drop_peer
  sent it nothing. When its game went on it read the host's older packets from its socket, so it believed it was still
  in the session (its own 30 s timeout, or 120 s with BUG-1419 while the host was in an editor) and played on a dead
  link: the host ignored its packets (a retired connection).
* A guest whose game stands while its window pumps messages (a dialog) keeps its link alive through the keepalive
  (coop.cpp, BUG-0687): it is never dropped. The host's vote then waited for its ready until the vote's 60 s: an
  action vote waits for a guest in its editor by design (BUG-1369), and a guest that froze while its own editor loaded
  had not told "editor" yet, so the evolution vote's BUG-1239 rule did not skip it either (the host's editor request
  waits behind a faded picture during that vote).
* Nothing else on the host waits for a guest's editor: the Civ arrival editors (civ_arrival.cpp: "nothing on the host
  waits for them"), the evolution vote skips a guest in its editor (BUG-1239), the arrival vote never waits for one
  (arrival_rule.h), a world share for a gone guest is dropped with it (BUG-0166).

## Fix
* `net.timeout_told` (net/session.cpp): an accepted guest dropped for silence gets a REJECT (REJ_KICKED, three
  unreliable datagrams as coop.kick's, Session::kTimeoutText) before its entry goes; PlayerLeave keeps reason 1. Its
  game reads it when it goes on and ends its session at once with that text; it may join again (its token is known).
  Every 0.7.x guest shows a REJECT's text (BUG-1108).
* `core.vote_stuck_kick` (vote.cpp vote_tick): a guest the vote waits for whose game sent no state for 30 s (the
  same 30 s as the session's) is removed with Session::kick and a text that says why ("your game stood N s (in its
  editor) while the vote ... waited for it: the host went on; join again"); a notice to the others; the vote goes on
  without it. coop.vote prints the count.
* The host's timeout for its guests stays 30 s (no reverse long wait).
Wire: no new message, no family bump (REJECT exists, every 0.7.x guest handles it; an older host sends nothing).
Off: as 0.7.16.

## Verification
Open, waiting for the lab run.
* Unit: tests/net_tests.cpp test_timeout_told (`--off timeout_told` FAILS: the guest stays connected after its
  stall): make test passes.
* Wanted PASS: `python3 tools/lab.py run editor_stand_guest --lanes N` (the guest in its own editor: a 50 s box while
  the host's action vote waits: removed within 40 s, the vote ends; a 45 s hang: dropped after ~30 s, told, rejoins,
  back in the host's world; the host's frames go on).
* Wanted FAIL: `editor_stand_vote_off` (SPORE_FIX_OFF=core.vote_stuck_kick: the vote runs its 60 s, the guest is not
  removed) and `editor_stand_told_off` (SPORE_FIX_OFF=net.timeout_told: the guest stays "connected" after its hang).

- 2026-11-23 (0.7.16.1 final check): editor_stand_guest PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened (the reverse case of PR #216).
- 2026-11-22: fixed on fix/w63-editor (35163c73), tests 2a454d1e.

- 2026-11-22: renumbered from BUG-9305 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9305.
- 2026-11-23: first lab run (final check 20261123-104630): editor_stand_guest FAIL "the guest is told why (its game stood while the vote waited)", link (resends/sent on the host), "guest: the console stopped answering". A test bug: kit.test.dialog shows its box from the frame's pump inside Present (watchdog chain), where the keepalive waits for the frame's end, so the guest sent nothing for 50 s; the host's session timeout dropped it ("timed out (no packets) ... last heard 29752 ms") and net.timeout_told told it ("rejected: the host heard nothing from this game for 30 s ...: join again"), 0.4 s after its game went on in part 2 as well. The guest did not crash: faults 0, it rejoined, left its editor and was back in the host's world; its console was held by the 50 s box command. fix/w68-triage 2bb64f64: the stall is `coop.stalltest 50` (outside the frame, the keepalive runs: the vote's kick is what removes it), the stands are declared (twoinst.stand), the link invariant allowed on the host; 1718b4c2 (MOD CODE, test command only): coop.stalltest's cap 20 -> 60 s. Verify: `python3 tools/lab.py run editor_stand_guest editor_stand_vote_off editor_stand_told_off --lanes 3` (the twins must FAIL).
