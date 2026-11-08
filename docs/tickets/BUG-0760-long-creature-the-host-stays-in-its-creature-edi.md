---
id: BUG-0760
title: long_creature: the host stays in its Creature editor after the editor event's red X (coop.vote editor has no red X since BUG-0720; b745a1f4 moved the host back to the red X)
status: fixed
kind: test
stage: Creature
reported: 2026-11-08
source: lab 20261108-114435 long_creature (rel/0.7.11 75365a87, lane 3)
role: host
versions: 0.7.11
fixed_in: unreleased
branch: fix/w46-final11-1
commits: 58c321d8
tests: long_creature, long_creature_short (tools/tests/coop_long_session.py)
signature: long_creature: `editor: NOT together after 240 s (host 0x00DBDBA1 Editor, guest 0x01654C01 Creature)`, `FAIL editor: the guest plays the host's world again`, `FAIL the games survived the session`; host log `busy=editor` to the end, no crash or hang
docs:
related: BUG-0720, BUG-0756
---

## Report
0.7.11 final check (lab 20261108-114435, long_creature, lane 3, 1317 s): the editor event (14.4 min in) failed and the
session stopped there ("the games survived the session" is the session's own flag: it fails when an event fails). No
game died or hung: the host's log goes on to the end with fps 65-67 and `busy=editor`
(long_creature.host.game.log 845-960); the guest left its own editor with the red X ("own editor result: ... cancelled 1",
long_creature.guest.game.log 824) and played Creature alone ("Alice (host) is in an editor"). The host's editor
(0x00DBDBA1) stayed open from s 846.6 to the end (~7 min): the test's red X click did nothing.

## Root cause
Observed (code, mods/coop/coop.cpp build_editor_request and `coop.vote editor`): since BUG-0720
(creations.editor_request) the test command `coop.vote editor` builds the host's request with the game's evolution
values (+0x39 = +0x3B = 0, +0x3C = 1): cEditor::OnEnter hides the red cross (window 0x90439D7C from +0x3B,
docs/re/creations.md#creations-own-editor-cancel). 9c13cf09 moved coop_long_session.py to the check mark (DONE) for that;
b745a1f4 (BUG-0756) gave the red cross back only to a guest's OWN editor (EditKind::Own) and moved long_session back to
the red X for the host as well, whose request (EditKind::Edit) still has no cross. Test-only: the game's own evolution
editor (the mate call) never had a red cross, in 0.7.10.1 either; 0.7.10.1's `coop.vote editor` set only +0xC, the key
and +0x94 (the constructor's +0x39 / +0x3B = 1: a cross), so the same test passed there (rel/0.7.10 lab
20261107-141346: the host left 10 s after the editor opened). Not a game or mod regression for players.

## Fix
tools/tests/coop_long_session.py: after the red X the host's editor is left with the check (DONE 1416,1062) when it is
still open (the editor event and leave_editors); the guest's own editor keeps the red X (BUG-0756).

## Verification
long_creature_short (the same five events, 6 min of play), the same build (rel/0.7.11 75365a87 DLLs):
FAIL with the old test (rel/0.7.11): out/lab/20261108-140201-rel-0.7.11 (lane 4: `FAIL editor: the guest plays the host's
world again (446 s)`, the host in 0x00DBDBA1 Editor); PASS with the fix (fix/w46-final11-1 58c321d8):
out/lab/20261108-140200-fix-w46-final11-1 (lane 3: all five events ok, the editor in 121 s). The original failure:
out/lab/20261108-114435 long_creature.

## History
- 2026-11-08: opened.
- 2026-11-08: fixed on fix/w46-final11-1 (test only; not a 0.7.11 regression for players).
