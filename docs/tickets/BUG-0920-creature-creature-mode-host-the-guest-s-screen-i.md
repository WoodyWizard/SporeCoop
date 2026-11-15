---
id: BUG-0920
title: Creature, creature.mode host: the guest's screen is black with no HUD 15 s after the host's evolution and the guest's own refused evolution request (one_creature, 3 of 4 runs)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: guest
versions: 0.7.12 (unreleased)
fixed_in: 0.7.12
branch: fix/w47-one-creature-black
commits: a9206649
tests: one_creature
signature: black screen, no game HUD, flag 0x1686AF0 = 1 in the Creature stage with no editor coming
docs: docs/re/creature.md#creature-editor-pending-flag, docs/06-testing.md#testing-screen-look
related: BUG-0769, BUG-0318, BUG-0508, BUG-0928, BUG-0929
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
Observed (lanes 9 / 10, `one_creature` with a screenshot measured after every step; objdump of the exe):
* The step: the guest's own evolution request that the mod refuses (step 5). After the swap to its own creature, to
  the host's creature, while the host is in its editor and after the swap to the host's evolved creature the picture
  was normal (world 103-107 of 255, HUD bar 44, minimap 100) and the game's byte 0x1686AF0 was 0; right after the
  refused request the picture is 0 / 0 / 0 and the byte is 1 (lab 20261111-063725, 20261111-064320). The body swap
  is not the cause.
* It does not come back: black after 60 s, after a held movement key (the creature does not walk either: position
  unchanged), after F9 twice. `mem.write 0x1686AF0 00` brings the world and the HUD back at once.
* Why: the creature stage's evolution request (handler 0xD42E50, the mating call) calls EditorRequest::Submit and
  then sets its "an editor of the stage is pending" byte 0x1686AF0 whatever Submit answered (0xD43069); only the
  next mode switch clears it (0xB5F19D); while it is set the frame function 0xB5EAE0 skips the stage's own drawing.
  The creature.mode host branch of the Submit hook (coop.cpp h_Submit) returns false and never submits the request
  later, so no mode switch follows and the flag stays. The same with the game's own request (`coop.vote mate`: the
  game's handler builds the request and sets the flag itself), which is what a guest's mating call does in play.
* The host's vote path also returns false in the hook but submits the held request when the vote ends: the editor's
  mode switch clears the flag.
Not explained: the one earlier run that was not black (feat/w46-settings-all lane 6, its logs are gone).
Details: docs/re/creature.md#creature-editor-pending-flag.

## Fix
`core.evo_flag_clear` (mods/coop/coop.cpp evo_flag_tick, every tick): when the game's editor-pending flag is set in a
stage and nothing will open an editor (no vote active, no held or queued request, no own editor due, no mode switch
pending, no request submitted to the game in the last 15 s) the flag is cleared and a log line written: on the next
tick after the hook's own refusal (creature.mode host), otherwise after 1 s of that state (a held request that was
dropped, a Submit the game itself refused). `coop.fix core.evo_flag_clear off`: the flag stays as before.
`coop.status` of the vote shows "editor-pending flag N, cleared M".
Side note 1 (the health 16/10): BUG-0928, fixed in the same commit (`creations.swap_health`).
Side note 2 (the tag "Bob" with no creature under it): not a timing of the puppet; the tag's height comes from the
bounding radius of a low, long creature: BUG-0929 (open, cosmetic).
New with creature.mode host, which ships first in 0.7.12: no CHANGELOG line.

## Verification
Test `one_creature` (tools/tests/coop_one_creature.py, lanes 9 / 10) now measures a screenshot of the guest's game after
every step (tools/tests/screen_look.py: the world not black, the HUD's bar and minimap drawn), has a step 6 with the
game's own request (`coop.vote mate`), and checks that the guest's creature walks after the refusals.
* FAIL: out/lab/20261111-064320 (`SPORE_FIX_OFF=core.evo_flag_clear`, lane 9): "step 5 ...: the guest's screen shows
  the world, not a black picture (world 0, bar 0, map 0)", the same for the HUD and for step 6, "the guest's creature
  walks after the refusals" (did not move); the probe: black 45 s later, after a movement key, after F9 twice; normal
  (world 110) after the flag was written to 0. Also out/lab/20261111-063725 (before the fix existed: steps 1-4 ok,
  step 5 black, the same probe result).
* PASS: out/lab/20261111-064331 (lane 10; the flag 1 right after the game's own request, cleared by the next tick,
  2 clears logged), out/lab/20261111-064714 (one_creature x5 on lane 9, 5 of 5 OK; world 106-109, bar 44, map 100
  after steps 5 and 6 in every run). The x5 run and 064331 ran on the working tree that became commit a9206649.
* The area on lane 10, out/lab/20261111-064941, 8 of 8 OK: settings_creature, editor_now, editor_now_off, switch_vote_off,
  vote, vote_drop_editor, vote_during_cinematic, evolve_in_place_creature; the new clear never ran in them (no log line).
* Key frame `evolved` of 20261111-064331, looked at: the guest's screen shows the world, both creatures and the HUD.

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-one-creature-black (core.evo_flag_clear); the step is the refused request, not the body swap.
- 2026-11-11: released in 0.7.12.
