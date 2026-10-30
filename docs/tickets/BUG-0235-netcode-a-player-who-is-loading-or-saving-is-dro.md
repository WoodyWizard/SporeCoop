---
id: BUG-0235
title: Netcode: a player who is loading or saving is dropped when someone with the same name joins; a crash rejoin into a full session is refused
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #169 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-net
commits: 57ca92f
tests: tests/net_tests.cpp test_rejoin_token (net_tests --off rejoin_token fails); lab nest_species, rejoin_loop, multi3 (regression)
signature:
docs: docs/04-netcode.md#netcode-hello-caps
switch: net.rejoin_token
related:
---

## Report
From GitHub issue #169: https://github.com/WoodyWizard/SporeCoop/issues/169 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0209 fix (commit 1d4cd27, release 0.7.3.0) decides that a new player is the same person as an existing entry when the two have the same name and the old entry has been silent for 2 s. Loading or saving can keep a player silent for longer than that. So with the default name "Player", a friend who joins while another "Player" is busy takes over that player's entry. The busy player is dropped by the host without being told, and only notices 30 s later.

The same fix leaves two gaps:
- a crashed player who rejoins a full 4/4 session is still refused ("session is full");
- a player who does rejoin sees a copy of their own old entry as a partner for a moment.

Severity: desync / wrong behaviour (a player is dropped from the session).

**Where**
- The name and silence check, r

## Root cause
Observed in the unit test (the issue's harness as test_rejoin_token): BUG-0209 took "same name and 2 s silent" for "the same game after a crash". A game blocked by a load or save is silent too, so a different player of the default name took over its entry (the busy player was dropped without being told). The rule ran after the full check (a crash rejoin into 4/4 was refused) and only flagged the old entry, so WELCOME still listed it.

## Fix
A rejoin token: `mods/coop/rejoin.cpp` keeps a random u64 in `<AppData>\SporeCoop\rejoin-<instance>-<slot>.txt` (profile-aware AppData, the first slot the game can open exclusively, kept open; a crashed game's handle closes so its restart takes the same slot and finds the token). `do_join` sends it in HELLO (caps bit 1); a clean leave, a session end and the unload empty the file. The host's HELLO handler, before the full check and WELCOME, drops (drop_peer: PLAYER_LEAVE first) an accepted entry with the same token that has been silent for 2 s; a HELLO without token (0.7.2, fakepeer) or a live entry never replaces anybody (newcomer gets "name 2"). Switch `coop.fix net.rejoin_token off` = the 0.7.3.0 name rule; `core.rejoin_name off` = never replace.

## Verification
`net_tests`: PASS (busy: the first "Bob" keeps its entry and its chat arrives, the newcomer is "Bob 2"; full 4/4: the rejoin is accepted as "Carol", 4 players, the others see L3 before J3, no own old entry; edge cases: no extension, live token, rejoin_name off all give "Bob 2"). `net_tests --off rejoin_token`: FAIL 7 checks (leaves=1, x took "Bob", "rejected: session is full"). Game regression PASS: lab run 20261028-230607 on lane 6 (nest_species: the restarted guest is "Bob" again through the token file; rejoin_loop, multi3, chat_long, cell_net, fix_registry). Not measured: how long a real load keeps a guest silent (test plan 3 of the issue).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
- 2026-10-29: released in 0.7.3.1.
