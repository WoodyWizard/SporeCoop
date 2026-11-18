---
id: BUG-9105
title: Lineages Cache: authors opt in before hosts pass their creations on
status: fixed
kind: feature
stage: All
reported: 2026-11-09
source: player request (roadmap 42, cache decision 5B)
role: host guest
versions: 0.7.3.6-lineages41-dev
fixed_in: 0.7.3.6-lineages42-dev
branch: feat/lineages-42
commits: 63abe7bf
tests: three-player QA (TITAN, second TITAN instance as host, MSI), see Verification
signature: -
docs: docs/re/creations.md#creations-cache-pass-on
related: BUG-9093
---

## Request

A player may opt in to let their creations be relayed beyond the people they played with (5B).

## Before

A host relays a received creation only to the players of the session it arrived in (`g_relay`,
cleared at session end); received creations are never offered later.

## Implementation (mods/coop/library.cpp)

- Choice: the Co-op window's Lobby, World & Session panel: "Share Onward: Off/On" (under Send
  World for the host, in its place for guests; hidden on the host while a vote's Ready button
  uses that row). Saved to coop.ini `[library] share_onward`; also `coop.library onward on|off`.
  Default off.
- Each player tells the host its choice in MSG_LIBRARY kind 5 (u8 5, str8 source identity,
  u8 allow). Older builds drop it: their Exchange::receive reads a hash after the kind and
  finds none. No new capability bit.
- The host applies an opt-in only when the same peer then offers a creation under that source
  identity (a guest offers only its own creations), so nobody can opt in someone else; an opt-out
  applies at once. Switched on during a session, it applies at once when that peer already
  offered creations under the identity this session. Opted-in sources are kept in LineagesCache\relay.bin ("LLR1", count, str8).
- As host of any later session, it offers the received creations of opted-in authors after its
  own (`offer_onward`); `eligible` accepts them. `coop.library` shows onward, onward_authors and
  onward_offered.

## Verification

no-fail-run: new feature (nothing to fail before it); checked by hand on the Windows QA copy below, no lab test yet

See History.

## History
- 2026-11-10: built on feat/lineages-42.
- 2026-11-10: three players, each step a fresh host session. TITAN QA (LineagesDepthQA) made
  a new creation (aaab8ff1..., unique to it). Host = second TITAN instance (LineagesDiff41Guest,
  port 47813); MSI QA (older 42 build, joins over Tailscale).
  1a: TITAN joined with onward off: host received TITAN's 7 unique creations, onward_authors=0.
  2a: MSI joined a new host session: onward_offered=0, the MSI got nothing new, aaab8ff1
      absent (negative control).
  1b: TITAN joined with onward on: host onward_authors=1 at once, relay.bin written.
  2b: new host session: onward_offered=7; the MSI imported aaab8ff1 (its only new entry,
      291D2627) within 20 s, "shared by TitanQA". 0 faults on all three games.
- 2026-11-10: lobby toggle (screenshots: host shows it under Send World, guest in its place).
  A guest switched it on mid-session: the host's onward_authors went 0 -> 1 at once. 0 faults.
- 2026-11-10: released in 0.7.3.6-lineages42-dev.
- 2026-11-10: superseded in 43-dev by BUG-9112 (Foreign Creations: the receiver decides, player decision).
