---
id: BUG-0431
title: class: requests and objects the mod builds are incomplete vs the game's own (umbrella)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content
commits: e81a56d9 ad15c36c
tests: editor_request, puppet_tuning
signature:
docs: docs/re/creations.md#creations-editor-request-builder
related: BUG-0720, BUG-0721, BUG-0722, BUG-0643, BUG-0644, BUG-0648, BUG-0368, BUG-0008
---

## Report
Fixed per case: BUG-0368 (EditorRequest +0x94), BUG-0008 (per-reader guards).
Siblings not covered: own editor request vs the game's (0xD42E50): +0x90 = 0x64AA168, bytes +0x39/+0x3B = 0 and +0x3C = 1, collectables vector cPlayer +0x10FC..+0x1108, message 0x5D02A72 after Submit, global 0x1686AF0 (obs); the coop.vote editor test command (coop.cpp:4831) same; Cell and Tribe/Civ/Space editors not checked; tuning readers unguarded for puppets/proxies: callers of 0xC0C300 (0xBA43E0, 0xBA4A00, 0xD9BBD0, 0xC09B40), direct read 0xC115E0 (obs static).
General fix: build requests like the game (byte diff with an allow-list); complete puppets/proxies (tuning block). Outcome check: request diff; every tuning reader runs on a puppet/proxy without fault.
Source: docs/generalisation-plan.md G15.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2b audit (0.7.11): the own-editor request differed from the game's at +0x39, +0x3B, +0x3C and the +0x70 traits vector (runtime byte diff); a guest's puppet playing its own species had no archetype tuning (+0xE84 = 0) on the other games; no other stage's editor request is built by the mod.

## Fix
Shared methods: one build_editor_request() for every editor request the mod makes, checked byte by byte against the game's own (coop.editreq diff, allow-list documented: +0x90 of the own editor, message 0x5D02A72, byte 0x1686AF0) (creations.editor_request); every puppet gets the avatar's tuning (npc.puppet_tuning). Children: BUG-0720, 0722 fixed, BUG-0721 not-reproduced (no request built outside Creature).

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2b final run on the merged branch out/lab/20261108-054842 (detect_* of all five stages, play_* of all five stages, every K2b test, SPORE_DETECTOR=20; report out of tools/detector_report.py): no signature of this class; the rows seen map to other tickets (BUG-0475, 0476, 0477, 0478, 0479, 0480, 0487, 0497, 0539, 0579, 0617, 0677, the BUG-0421 egg / guest-local rows, and Civ planet village / industry rows of K8's BUG-0659).

## History
- 2026-11-01: opened.
- 2026-11-06: Discord report 2026-11-06 (0.7.8): child BUG-0644 (NPC villages the guest makes get their own colours); related BUG-0648.
- 2026-11-08 (fix/w46-k2b-content, K2b of 0.7.11): every sibling item a closed child or covered; umbrella closed.
