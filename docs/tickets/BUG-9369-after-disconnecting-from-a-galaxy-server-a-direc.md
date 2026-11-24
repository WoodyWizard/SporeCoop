---
id: BUG-9369
title: After disconnecting from a galaxy server a direct game cannot be hosted or joined
status: fixed
kind: bug
stage: Galaxy
reported: 2026-11-24
source: deep playtest 2026-11-24
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 22b04cce
tests: tools/qa_win (manual, TITAN + the MSI): a galaxy planet, coop.galaxy disconnect, then coop.host / coop.join
signature:
docs: docs/12-server-and-players.md#server-direct-after-disconnect
related:
---

## Report
Deep playtest 2026-11-24: after `coop.galaxy disconnect` (both players had been on a galaxy planet) the host's
`coop.host 47839 ...` answered "[not ok] host failed: not connected to a galaxy server" and the other game's
`coop.join` never connected. Only a restart of the game gave a direct game back. A player who leaves the galaxy to
play a direct session (an adventure, a friend's game) with the Co-op window's own buttons hits the same.

## Root cause
Observed (code): galaxy_client.cpp leave_now() ends the planet's session and leaves its relay transport on "until
the session is closed, tick switches it off"; disconnect() closes the socket right after it, and tick() returns at
once while the socket is closed. The session kept RelayTransport, whose open() fails without the galaxy socket.

## Fix
galaxy_client.cpp disconnect(): after leave_now() the session ends ("left the galaxy") and goes back to the plain
network (use_relay(false)) before the socket closes. Switch `coop.fix galaxy.direct_after_disconnect off`: as before.

## Verification
- FAIL (the build before, 17:53, TITAN): after a disconnect `coop.host` -> "[not ok] host failed: not connected to a
  galaxy server"; the MSI's join showed "joining ..." and stayed offline.
- PASS (the fix, 18:10): both on Talas (Civ), `coop.galaxy disconnect` on both (the host stores its world first),
  `coop.host 47839` -> "hosting on UDP 47839", the MSI's `coop.join` -> "host port=47839 players=2/4", "client id=1
  connected"; the session then ran a two-player adventure (images/deep_adventure_host.png, _guest.png).
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the run of the build before.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the deep playtest).
