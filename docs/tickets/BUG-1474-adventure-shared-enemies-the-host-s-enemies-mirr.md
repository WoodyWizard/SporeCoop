---
id: BUG-1474
title: Adventure: shared enemies (the host's enemies mirrored on the guest, off-by-default adventure.shared_npcs; RE first)
status: fixed
kind: feature
stage: Adventure
reported: 2026-11-23
source: plan
role: 
versions: 0.7.17
fixed_in: 0.7.99
branch: rel/0.7.99
commits: c6d51d00, ff9422e5, 2aaf5dd6, 40ef2081, f92ec15d, a006bdcd, 251c06c6
tests: adventure_enemies, adventure_host_actors_off, adventure_hit_forward_off, adventure_guest_extras_off, adventure_body_side_off, adventure_duo
signature:
docs: docs/re/adventure.md#adventure-npc-proxies
related: BUG-0793, BUG-1288, BUG-1254, BUG-1262
switch: adventure.host_actors, adventure.hit_forward, adventure.guest_extras, adventure.body_side
---

## Report
Each game plays its own copy of an adventure (docs/re/adventure.md#adventure-two-captains): the enemies are each game's own. RE first (the adventure's creature list and act spawns, whether the Creature NPC proxy path works in kScenario), then host-authoritative enemies behind `adventure.shared_npcs`, test coop_adventure_tutorial.py act 1: docs/plan-0.7.17.md item 6. Scope per owner question 6.

## Root cause
Design question, RE first (static only, GOG 2024 Ghidra, 2026-11-23; no lab by day): docs/re/adventure.md#adventure-npc-proxies.
Verdict: the Creature NPC proxy path (npc.cpp) does **not** work in kScenario as it is.
* Observed (disassembly, the same pass as BUG-1254's runtime fault): a proxy is made by Create in a nest herd or a herd
  of ours, without a scenario object (cGameData +0x1C -1). The next act change (SetCurrentAct 0xF1EE70 -> the act pass
  0xF1E580) runs Creature_ScenarioActSetup 0xC070D0 on every creature outside the avatar's herd and faults at 0xC0714E
  on it, as the BUG-1254 puppet did on both games.
* Observed (disassembly): the guest's update filter would remove the adventure's own creatures: objects with a marker
  (+0x18) and a class (+0x1C), held in the scenario data's marker maps (0xF3D620 / 0xF3DB10), counted by the spawners
  (0xF24240) and targeted by the guest's goals by class (goal +0x28). A proxy has no class: the guest's defeat goals
  never count it and its own act changes and spawners make the creatures again (likely: the fork saw 80 unpaired and
  56 at 0 hp after 3 minutes).
* Observed (disassembly): acts, goals and the end (ScenarioCreditGoal 0xF1ACF0, SetState 0xF1A9C0) are each game's
  own, so even with proxies the guest's run would not follow the host's kills.
* Inferred: proxies in the avatar's herd (the BUG-1254 cure) avoid the fault but are the captain's own family with the
  default attitude 2 (0xC04930), still without a class; not checked at runtime.

## Fix
None in 0.7.17: per the provisional owner choice of question 6 (docs/plan-0.7.17.md: an off-by-default option only if
the RE shows the Creature proxies work there) the feature **moves to 0.8**, as part of co-op adventures (BUG-0793,
docs/11-rivals.md). No `adventure.shared_npcs` setting and no code change; RE notes and 12 symbols only
(data/symbols/overrides_dc04aee5.sym, the feat/w83-advnpc entries; ScenarioCreditGoal now checked by us).

Proposal for 0.8 (the fork's E-A + E-B, out/research/lineages-systems.md section E, ported in our style):
1. Runtime RE first (one lab night, the tutorial and one combat adventure): the marker and class of the same creature
   on both games, the pairing distance, the death through the behaviour tree (a creature with its AI skipped only goes
   to 0 hp), the political id cGameData +0x30 of a partner body.
2. E-A shared run (M): a host journal of goal credits (hook ScenarioCreditGoal 0xF1ACF0; defeat credits of a named
   object stay native), act changes and the end (hook SetState 0xF1A9C0: the host's 5 / 6 decide, a guest's own 6 is
   held), late join resend; setting `adventure.shared` (off = each game its own copy, as 0.7.17).
3. E-B host-run creatures (L): the guest keeps its adventure objects and pairs each host creature with its own of the
   same marker + class (nearest unpaired within 12 m), skips only their NPCTickAI, drives them with the puppet driver,
   mirrors the host's deaths through Die, forwards its captain's hits to the host (npc.cpp's MSG_DAMAGE_REQ idea, a
   new message id from 79 on and a new `adventure` family minor), removes its unpaired leftovers after 5 s and gives
   the AI back when the host's stream stops; the host's `ai_target` runs in kScenario for partner bodies, which get
   their captain's side (+0x30). Switches `adventure.host_actors`, `adventure.hit_forward`, `adventure.guest_extras`,
   `adventure.body_side`; npc.cpp's spawn-and-remove layer is not used.
4. Tests: coop_adventure_tutorial.py act 1 (Skex and Thorns) for E-B (paired positions, a guest's hit lowers the host's
   creature's health read on the host, a host kill dead on the guest's game, key frames of the same view), a combat
   test adventure in the test profile for E-A (goal counts and the end on both games), each with its FAIL twin.
Credit: the design of the Lineages fork (PR #196, PandizeLive) where it is ported.

## Verification
PASS: out/lab/20261124-162401 (adventure_enemies, 0 faults), out/lab/20261124-152417 (adventure_duo, 0 faults).
FAIL: out/lab/20261124-145540 (adventure_host_actors_off, adventure_guest_extras_off), out/lab/20261124-162407 (adventure_hit_forward_off, adventure_body_side_off).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 6 (WP-A1), branch feat/w83-advnpc.
- 2026-11-23: RE on feat/w83-advnpc (WP-A1, static): the Creature NPC proxies do not work in an adventure (act pass fault at 0xC0714E, the guest's adventure objects removed, goals by class); no setting in 0.7.17, moved to 0.8 with BUG-0793 (proposal above); waits for the owner's answer to question 6.
- 2026-11-24: released in 0.7.99.
