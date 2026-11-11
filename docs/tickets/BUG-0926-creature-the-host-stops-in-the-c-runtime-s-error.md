---
id: BUG-0926
title: Creature: the host stops in the C runtime's error box ('Wine C++ Runtime Library') 2 s after the partner's creature died and its puppet was removed (long_creature)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-11
source: 0.7.12 final check, lab 20261111-044257 long_creature (lane 3)
role: host
versions: 0.7.12
fixed_in: unreleased
branch: fix/w47-final12
commits: 65d55a4b
tests: herd_help_stale, long_creature
signature: dc04aee5:SporeApp.exe+0x98F2AD (R6025 pure virtual call under Wine / 0x40000015 on Windows, BT_HerdHelpEval reading stimulus +0x10)
docs: docs/re/creature.md#creature-stimulus-attacker
related: BUG-0456, BUG-0453, BUG-0918, BUG-0925, BUG-0930, BUG-0687
---

## Report
The 0.7.12 final check's long_creature (lab 20261111-044257, lane 3, 35 min into the session): "host: FROZE", faults 0.
The host's game showed the C runtime's error box ('Wine C++ Runtime Library') 2 s after the partner's creature died
in a hunt and its puppet was removed (`Bob hidden: flags=0x11`, `6 stimulus(es) of NPCs named ... cleared`). For a
player: the host's game ends with a runtime error (on Windows: it closes, crash code 0x40000015) a moment after the
partner's creature dies in a fight with a pack, mostly when the prey was killed in the same second.

## Root cause
Observed:
* With BUG-0925's diagnostics the stop came again in lab 20261111-064457 (long_creature_short#1, lane 5, 77 s in, 0.4 s
  after the partner's death): the box's text is `Runtime error! ... Error: R6025` (a pure virtual function call) and
  the main thread's chain is msvcr90 <- 0xD8F2AD (`BT_HerdHelpEval`+0xED) <- BehaviorTreeTick <- NPCTickAI <-
  cCreatureAnimal::Update: BUG-0456's own signature.
* BT_HerdHelpEval 0xD8F1C0 (decompiled): it finds the "attacked" stimulus 0x200, checks that its SENDER (+0x2C, via
  0xD998E0) lives, and then calls the creature at stimulus **+0x10** (`+0xC0` vt 0x58, `+0x5A8` vt 0xC at 0xD8F2AB).
  +0x10 is the attacker, a plain pointer without a reference; the sender is the herd mate that was attacked.
  BUG-0456's scrub (npc.stale_sender) cleared stimuli by their sender only.
* `coop.staleref` (new, a scan of the process for pointers into a destroyed creature): after the puppet's destroy the
  AI blocks of 1-3 NPCs hold it at +0x168 / +0x1A0 (stimulus slots 6 / 7, +0x10) in 5 of 6 removals.
* The destroyed puppet's block is not freed at once: it keeps the creature's vtables and the destroyed flag while
  references remain (23 -> 1 within a second). The NPC it hit last keeps one (its last attacker, combatant +0x28)
  for as long as it lives; with that NPC killed just before (the hunt's kill, credited to the host's creature) the
  count reaches 0 and the block is freed 0.33 s after the destroy (lab lane 2, `coop_stale_refs.py --kill-first`).
  The next herd-help evaluation of a herd mate then calls freed memory: R6025 in round 0 of every such run.
  That is why `npc_lost_target` never met it (its NPC lives on) and the long sessions met it rarely.
Inferred: BUG-0456's player crash (0.7.3.4) was this path too (same address; its dump showed a destructed object,
and a sender holds a reference, so it cannot have been the sender).
Not a regression of 0.7.12: the path is as old as the puppets (BUG-0456 fixed the sender half in 0.7.4). The
suspects of the brief did not take part: `game.avatar_stale_target` did not fire at that destroy, no proxy, Creature
stage; the play bots only made the hunts that kill the prey as the partner dies. BUG-0687 (0.7.10) was not a dialog
of this kind as far as its log shows (no #32770 in a window list: it predates the note); it stays not reproduced.

## Fix
mods/coop/gamefixes.cpp `scrub_stimuli` (the destroy hook of BUG-0456): a stimulus whose +0x10 names the creature
being destroyed is cleared as a whole, like one sent by it (masks, value, the sender released, the union rewritten);
+0x10 is also zeroed in slots that are not live. Counter in `coop.status` ("as the attacker N").
Switch: `coop.fix npc.stale_attacker off`.

## Verification
Test `herd_help_stale` (tools/tests/coop_herd_help_stale.py, lane 2): the guest fights an NPC, the host's creature
kills it, the puppet is removed; the NPCs' stimuli are read from the host's memory and the host must go on drawing.
* FAIL: out/lab/20261111-071043 (`SPORE_FIX_OFF=npc.stale_attacker`): 3 stimuli named the removed puppet as the
  attacker, the host stopped 2 s after the first removal (the error box, a report). Also out/lab/20261111-064457
  (long_creature_short#1 before the fix: R6025 with the chain) and the final check's out/lab/20261111-044257
  (rel/0.7.12 worktree).
* PASS: out/lab/20261111-071141, out/lab/20261111-071336 (6 removals each, 0 stimuli, no stop).
The long sessions and the area's tests: see History.

## History
- 2026-11-11: opened.
- 2026-11-11: root cause (the attacker field +0x10 of the herd's "attacked" stimuli), fixed on fix/w47-final12 (npc.stale_attacker).
- 2026-11-11: runs with the fix (fix/w47-final12, `SPORE_DETECTOR=20`). Build 65d55a4b: long_creature_short x3 out/lab/20261111-071520 (OK), the area out/lab/20261111-071522 (11/11: npc_lost_target, creature, creature_kills, avatar_stale_target, team, dialog_note, keepalive_stall, cell tests), campaign_short out/lab/20261111-071826 (OK), long_creature out/lab/20261111-071518: the session survived (46 min, no stop of the host), failed on the guest's BUG-0931 and one sync-check (BUG-0932). Build fb8b2e2f (with BUG-0931): long_creature out/lab/20261111-080711 (lane 3, 45.6 min, OK) and out/lab/20261111-080715 (lane 2, OK), long_creature_short x3 out/lab/20261111-080713 (OK), the area + play_creature + campaign_short out/lab/20261111-080717 (9/9).
