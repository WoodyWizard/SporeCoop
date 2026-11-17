---
id: BUG-9251
title: Host: no way to remove a player (kick) or keep one out (ban)
status: open
kind: bug
stage: all
reported: 2026-11-17
source: owner 2026-11-17 (decision 5I A)
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: manual Windows QA with tools/qa_win/qa.py (TITAN host 9 + partner 8, the MSI as player 2): coop.say /kick|/ban|/unban, coop.kick
signature:
docs: docs/04-netcode.md#net-kick-ban
related:
---

## Report
Owner 2026-11-17, decision 5I: "5IA - with kick and ban". A host had no way to remove a player from its game or to keep one out.

## Root cause
Missing feature: the session dropped a peer only on its goodbye or a timeout.

## Fix
Session::kick (MSG_REJECT with the reason, the peer dropped 1 s later); mods/coop/bans.cpp: the host's chat commands
/kick, /ban, /unban, /bans (console coop.kick|ban|unban|bans), the ban list SporeCoop\bans.txt in the host's profile
(identity, name, date), and a check once a second that removes a banned player as soon as its identity arrives. The
removed player's feed shows the host's words ("The host removed you from the game.") without "disconnected: rejected:".
Switch: `coop.fix core.bans off` (banned players are let in; kick still works).

## Verification
PASS (11:00-11:08): `/kick Titan` by the host: the partner left with the reason and stayed out until it joined
again (allowed); a partner's `/kick MSI`: "Only the host can remove players."; `/ban MSI`: the MSI removed, bans.txt
written; its next join removed 0.34 s after the join; `/unban MSI`: it joined and stayed. One line per action in the
host's feed after the fix of a double line.
FAIL (switch off, 11:03): the banned MSI joined and stayed connected; switched on again: removed within 3 s.
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-17: opened and built (decision 5I A), tested on TITAN and with the MSI; unreleased.
