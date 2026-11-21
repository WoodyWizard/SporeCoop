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
commits: 3c2a6550, 59d47c0a, db507eef, 8bd69ecc
tests: manual two-game MSI run (ticket Verification)
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
s in a row and with no loading cover of ours up, its game plays TRG_Upgrade outside the frame (the cinematic the host's
game plays after the piece; it closes after 8 s, BUG-0983), switch `tribe.partner_totem_cutscene`, OFF by default:
see Verification (the framing is wrong). A partner founder totem's pieces also count in the host's "campaign changed"
check (tribe.cpp, the win's share), so a catch-up after the win's share sends one more share. The party wipe (BUG-9155) drops the top piece from every founder totem, so the
counts stay matched. Readout: `coop.tribe totems` last line `partner_totems fix= made= pieces= fails= cutscenes= seen=
own_totem= own_pieces= last:`; logs "tribe: seat N's village got a totem", "seat N's village totem a -> b pieces",
"this village's totem has n pieces: the totem cutscene TRG_Upgrade plays here". Off: the host's totem only (67-dev).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (fix/l47-tribe, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a live
  Creature -> Tribe entry from Vandh; an NPC village by `coop.tribe npc_tribe 3 130`, the win by `coop.tribe win 2295`,
  the reward by opening and closing the host's planner):
  * FAIL off (first session, build before the switch; and 10:26 with `coop.fix tribe.partner_totems off` on both):
    host totem pieces 0 -> 1 and TRG_Upgrade on the host, `seat 1: no totem`; the guest played no cinematic, reloaded
    the win's share and its village still had no totem (out/qa-shots/bug9318_guest_village_before.png,
    bug9318_guest_after_win_off.png).
  * PASS on (11:11, and again 11:24 on the final build): host log "seat 1's village got a totem", "seat 1's village
    totem 0 -> 1 pieces (catch-up)" (the totem the hook made at the reward had no model slots yet, so the 5 s catch-up
    added the piece), `coop.tribe totems` seat 0 and seat 1 both "pieces 1 colours 1"; the host's share at 11:12:03
    ("saving for the share", the new check) and the guest's reload: its own village now shows the totem with the won
    village's pink piece (out/qa-shots/bug9318_guest_after_win_on.png); guest `coop.tribe totems` own_pieces=1.
  * Cutscene: the guest played TRG_Upgrade at 11:12:35, but under our reload cover (out/qa-shots/
    bug9318_guest_cutscene_under_cover.png); fixed to wait for the cover; at 11:25:25 it played after the cover, and
    showed about a second of sky with a spear's tip (bug9318_guest_cutscene_on_1.png), not the totem; `coop.tribe cine
    TRG_Upgrade` by itself on a guest: about 1 s of black sky, then the normal view. The host's own TRG_Upgrade is a
    close-up of its totem with the caption "You have made the Cyan Village your allies! ... shared a totem piece ..."
    (bug9318_host_native_totem_cutscene.png). Diagnosis: the cinematic needs the context its native caller
    (TribeHut_Upgrade 0xCDA4F0 from the reward queue 0xCDAC90) sets before playing it; played alone it frames nothing.
    So the guest's cutscene is behind `tribe.partner_totem_cutscene` (off) until that context is found.
  * Seen on the way: one host's pieces were dropped by repeated party wipes in a raided test world (BUG-9320).

- Live QA 2026-11-21, fix/l47-tribe2 (MSI two games, Vandh): the context of TRG_Upgrade, read from memory
  (docs/re/tribe.md#tribe-wipe-regroup): TribeHut_Upgrade 0xCDA4F0 is thiscall(TribalGame_Get()) on GetPlayerTribe()
  (a guest's own village); it plays TRG_Upgrade and binds its actors (the members near the hut in a formation, the
  village, the hut, the food mat, one more village object). The guest's cutscene now runs that upgrade instead of the
  bare PlayCinematic (`coop.tribe cine upgrade` by hand). A win (`coop.tribe win 2295`, the host's planner opened and
  closed) gave the guest's village its totem piece through the share (`own_pieces=1`) and the guest ran it by itself
  ("the hut upgrade ran for village 0000228C (cinematic now 94D17D9F)"). Result: no more sky; the screen fades to black
  for about a second, then shows the normal view of the guest's own village (its hut upgraded, the totem with the
  piece) with the HUD while the input stays locked until our 8 s closer ends it (BUG-0983); no close-up of the totem
  and no caption as on the host (out/qa-shots/bug9318_guest_hut_upgrade_path.png). So `tribe.partner_totem_cutscene`
  stays OFF: an 8 s input lock with no visible cutscene is worse than none. ? What else the host's reward path sets
  (TribeTokens +0x1C, the caption's village, written by 0xCDAC90 before the upgrade) and whether the guest's camera
  code of ours overrides the cinematic camera: not found.

## History
- 2026-11-21: opened.
- 2026-11-21: fix/l47-tribe: partner totems (live PASS on / FAIL off); the guest cutscene diagnosed, off by default.
- 2026-11-21: fix/l47-tribe2: the guest cutscene runs the game's hut upgrade (its actors); no sky now, but no totem
  close-up either: still off (Verification).
