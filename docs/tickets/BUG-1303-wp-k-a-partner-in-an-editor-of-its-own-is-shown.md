---
id: BUG-1303
title: WP-K: a partner in an editor of its own is shown in the game's cocoon where it stood and comes out with its new body (it disappeared before)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: plan
role: any
versions: 0.7.15.2 and older
fixed_in: 0.7.16
branch: feat/w56-cocoon
commits: d86d5d22 9320074b cc4dc305 b67a4a56
tests: cocoon, cocoon_guest, cocoon_off
signature:
docs: docs/re/creature.md#creature-cocoon, docs/06-testing.md#testing-cocoon
related: BUG-0516, BUG-0923, BUG-0451, BUG-1304, BUG-1305
---

## Report
Plan 0.7.16, WP-K (owner approval 2026-11-19): in the Creature stage, while a player is in an editor of its own (the
host's mating editor, a guest's "Evolve now" editor or its later editor at the nest), every other game should show
that player's creature wrapping into the game's own cocoon and holding in it at that spot; back, the creature stays in
the cocoon until it wears the new design, then comes out. The Lineages fork (PR #196, its BUG-9160 / 9181 / 9194) did
this.

## Root cause
Observed (0.7.15.2, rel/0.7.15.2 lab run 20261118-144132 editor_now, the guest's log): the partner's state in its
editor is mode Editor, the same world crc, flags 0; drive_puppet hides a partner that is not in the world, so its
creature vanished ("Alice hidden: flags=0x0 ... same_place=0") and a new puppet appeared when it was back ~18 s later
("Alice appeared in the world"). (The plan's text "the puppet just stands and then swaps its body" was not what the
lab shows: it disappeared.)

## Fix
`mods/coop/cocoon.cpp` (cocoon.h), wired in puppets.cpp drive_puppet / despawn / forget_puppets / on_remote_anim and
game.cpp (game::hold_pose: the idle refresh is skipped for a held body). A partner whose state says it is in an editor
in this world keeps its puppet, which plays the game's cocoon (docs/re/creature.md#creature-cocoon); back, it stays in
the cocoon until it wears the partner's new creature (a new body goes into the same cocoon), then emerges. No new
message: a 0.7.15.2 partner is shown in the cocoon too, a 0.7.15.2 game hides the partner as before. Effects released
with the puppet and only in the mode / world they were made in. Switch `coop.fix creature.cocoon off`: hidden as
before.

## Verification
Lab lane 6 (feat/w56-cocoon):
* First run 20261119-110736 (cocoon): FAIL on the animation checks only: the game answers 0 to the three cocoon
  animations (gen_evolve_cocoon 9C26F705, ..._hold 4E0D3CBF, gen_evolve_emerge D4F48A35) on GOG 2024 for the Creature
  stage's avatar and puppets (probe `coop.cocoon play <hex>` on lane 6: 0265D18B answers 259 and plays, the three answer
  0 by PlayAnimation and CreaturePlayAnim); the shell and glow were active, the body shrunk, the emerge with the new
  body ran. The test's animation check asserted what this game cannot do: it now checks the game's effects (shell,
  glow, burst IsActive), the body's scale and spot, the size back and the new body; the animations are printed
  (CORRECTION in docs/re/creature.md#creature-lineages-cocoon-mate). The mod asks for the animations once and then
  only shrinks and holds the body (game::place instead of teleport: no restart of the locomotion's animation).
* cocoon_guest 20261119-111735 FAIL: a test bug: the guest's `coop.vote evolve` opens the game's evolution editor,
  which has no red cross (BUG-1222); the test now leaves it with done.
* PASS: cocoon 20261119-111735 (host editor, new species 065CBE70 -> 2928F2E2, came out with 7CD5269E, burst
  active, size 1.153 back); cocoon_guest 20261119-112614 (no design: out after 6 s with the same body); key frames
  cocoon_hold (the woven cocoon named Alice / Bob where it stood), cocoon_emerge (the burst, the creature growing out),
  cocoon_after reviewed. Final tree b67a4a56 (rel/0.7.16 merged): cocoon 20261119-113606, every check of the test ok
  ("RESULT: OK" in its log); the run's verdict FAIL is only the lab's DNA gates (sync-check / monitor totals: host 15,
  guest 40 after the host's editor price), the own-DNA gate class of the night run (BUG-1291's area), not this fix.
* FAIL twin: cocoon_off 20261119-112934 (no puppet while the host edits: "still shows ... (None)", 0 samples).
  (That run's DNA gates, host 15 / guest 40 after the host's spend, are the own-DNA gates of BUG-1291's area, not this
  switch's.)

## History
- 2026-11-19: opened.
- 2026-11-19: implemented on feat/w56-cocoon (build, make test); lab pending.
- 2026-11-19: lab lane 6: the cocoon animations are not in the game here (CORRECTION), checks on the effects; fixed.
- 2026-11-23: released in 0.7.16.
