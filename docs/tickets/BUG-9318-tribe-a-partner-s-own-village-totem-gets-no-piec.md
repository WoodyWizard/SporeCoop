---
id: BUG-9318
title: Tribe: a partner's own village totem gets no piece for the party's wins and no totem cutscene
status: open
kind: bug
stage: Tribe
reported: 2026-11-21
source: owner 2026-11-21
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-tribe
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-partner-totems
related: BUG-9155, BUG-9158, BUG-0531, BUG-0983
---

## Report
Owner 2026-11-21: "our party member didn't see their totem gaining progress, only the host's tribe ... This plays into
the party member also not seeing the totem addition cutscene."

## Root cause
Observed live (MSI two games, fix/l47-tribe, 2026-11-21) and in the code / earlier RE (docs/re/tribe.md#tribe-totem-model-slots):
* The game's reward queue (0xCDAC90, run by TribeGame_ModeChanged after a queued win) adds the won village's piece
  (Totem_AddSegment 0xC8DDA0) to the PLAYER tribe's totem only: the host's village. A test win (`coop.tribe win 2295`
  on the host, then the planner opened and closed): host `coop.tribe totems` seat 0 pieces 0 -> 1, and the host played
  TRG_Upgrade (94D17D9F) after the piece; seat 1 "no totem".
* A partner founder village has no totem object at all (`seat 1: no totem` from the Tribe entry on): nothing made one
  at its founding, so there was nowhere for a piece to go, on the host or (after the win's share reload) on the guest.
* The guest's game played no cinematic for the win (`coop.status` showed none in 25 s); it showed "Saving Tribal
  conquest for your partner..." and reloaded the host's save 35 s later (its progress bar section filled, its hut
  upgraded by that save), so a piece added on the host reaches the guest through that save.
* `coop.tribe cine TRG_Upgrade` on the guest before the fix: a short camera move to the sky over its own village (no
  totem to look at), then back.

## Fix
Switch `tribe.partner_totems` (tribe.cpp; on). Host: right after the game adds a piece to the host's player totem
(h_AddSegment), and as a catch-up every 5 s (outside the frame, not while saving, loading or in a cinematic), every
partner founder village gets a totem from the game's own factory Tribe_CreateTotem (0xC992D0) when it has none, and
the host's pieces it lacks (the same kind and colour, Totem_AddSegment), only when its pieces are the start of the
host's and never past the model slots (BUG-9158). The win's share save carries them to the guests. Guest: when its own
village's totem (the native player tribe on a guest, tribe.own_player_village) has more pieces than it last saw, for 3
s in a row, its game plays TRG_Upgrade outside the frame (the cinematic the host's game plays after the piece; its
caption closes after 8 s, BUG-0983). The party wipe (BUG-9155) drops the top piece from every founder totem, so the
counts stay matched. Readout: `coop.tribe totems` last line `partner_totems fix= made= pieces= fails= cutscenes= seen=
own_totem= own_pieces= last:`; logs "tribe: seat N's village got a totem", "seat N's village totem a -> b pieces",
"this village's totem has n pieces: the totem cutscene TRG_Upgrade plays here". Off: the host's totem only (67-dev).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
