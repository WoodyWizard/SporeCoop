---
id: BUG-0920
title: Creature, creature.mode host: the guest's screen is black with no HUD 15 s after the host's evolution and the guest's own refused evolution request (one_creature, 3 of 4 runs)
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0769, BUG-0318, BUG-0508
---

## Report
Key-frame review of the final check out/lab/20261111-044257 (rel/0.7.12 2f8f6ba4), test one_creature (lane 4, result ok).
Sheets: `out/lab/20261111-044257/frames/sheets/028_one_creature_02_evolved.jpg` (the finding) and `out/lab/20261111-044257/frames/sheets/027_one_creature_01_host_creature.jpg` (before).
Frames: `out/lab/20261111-044257/frames/one_creature/02_evolved_host.jpg`, `02_evolved_guest.jpg`.

* Host (02 evolved): Creature play as expected: its new creature and the guest's copy at the nest, HUD, minimap, the
  tag "Bob".
* Guest (02 evolved): the whole picture is black. Only the mod's own overlay is drawn (the co-op status lines and the
  tag "Alice" in the middle of the screen); the game's HUD (minimap, ability bar, DNA bar, mission panel) is not drawn
  at all. `game.mode` answers Creature and `coop.avatar` is ok, so the test's check "the guest plays on" passes.
* When: the frame is shot 15 s after the guest's own `coop.vote evolve` (step 5 of the test), which the mod refused
  (guest log 05:47:01.950 "evolution: creature.mode host: this guest's editor request is refused (the host evolves)"),
  and 16 s after the guest's avatar got the host's new species (05:47:01.331 "own avatar species 065CBE70 -> 291E1916").
  The guest's trace lines in that time show no busy flag (no cinematic, no input lock, no pause).
* Not a single run: the test's own screenshot `one_creature_guest.png` (720x540, shot just before the key frame) has a
  mean brightness of 0.5 of 255 in this run (out/lane4/shots), in the rel/0.7.12 run of 2026-11-10 20:39 (out/lane9/shots)
  and in feat/w46-settings-all lane 7 (2026-11-10 19:07); it was a normal picture once (feat/w46-settings-all lane 6,
  19:13, brightness 88). 3 of 4 runs black.
* Whether the picture comes back later is not known: the test ends right after the frame.

Side observations of the same test (no ticket of their own):
* Guest log: `hp=16/10` on the guest's avatar after the switch from its own creature (16 hp) to the host's (max 10), and
  still 16/10 after the evolution: the health is not clamped to the new body's maximum. Sheet 027 shows the bar "16"
  with the green regen crosses over the guest's own creature.
* Sheet 027, host: the tag "Bob" stands about 1.5 m to the right of the two creatures at the nest with no creature
  under it, while the notice "Bob appeared in the world" is still sliding in: the puppet was made again a moment
  before (the species switch) and is not drawn yet. Timing of the frame, cannot be judged from one picture.

## Root cause
Not analysed (review only, no game runs). Inferred, two readings:
* The refused request: `coop.vote evolve` on a guest runs the game's own evolution request (the mating call's path).
  The game may darken the screen and hide its HUD for the editor before or while it submits the request; the F5
  branch in mods/coop/coop.cpp (the editor request hook, `g_one_refused`) returns false and nothing brings the picture
  back. The host's vote path also returns false there, so if this is the cause the difference is in what follows the
  refusal (the vote later submits the request again; the refusal never does).
* The body swap: the avatar's species changes twice in 0.6 s around the host's return (the host's new creation
  arrives, then the own avatar becomes 291E1916); the black, HUD-less picture is what BUG-0318 looked like (an editor
  opened while the new body was being built), here without an editor mode.
The lane 6 run that was not black is the hint to compare logs with.

## Fix
Open. New with F5 (BUG-0769, creature.mode host), which ships first in 0.7.12.

## Verification
Open. How to test: one_creature with a brightness check of the guest's screen after step 4 (before the guest's own
request) and again after step 5: that tells which step makes it black; then wait 60 s and press a movement key to
see whether it stays. The check the test lacks: "the guest's screen is not black" (mean brightness of a screenshot,
as coop_editor_after_swap.py does for BUG-0318), and the same after a real mating call of a guest in creature.mode
host. Also check `hp` against the new maximum after the switch.

## History
- 2026-11-11: opened.
