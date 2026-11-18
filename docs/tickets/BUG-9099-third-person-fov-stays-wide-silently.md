---
id: BUG-9099
title: Creature: the third-person FOV widening stays on silently and shifts the zoom range outward
status: fixed
kind: bug
stage: Creature
reported: 2026-11-09
source: player report on the personal Steam install (40-dev)
role: local (host)
versions: 0.7.3.6-lineages30-dev to 0.7.3.6-lineages40-dev
fixed_in: 0.7.3.6-lineages41-dev
branch: fix/fov-stuck-wide-lens
commits: 04ad56ba 83c0a710
tests: live console check (coop.fov) on the personal Steam game, see Verification
signature: coop.fov reports steps=6 while the player believes the camera is vanilla
docs: CHANGELOG.md
related: BUG-0177
---

## Report

The player could no longer zoom the Creature camera in to the close, first-person
like view, and could zoom out much farther than before. "It is almost like the
zoom default moved, while keeping the range the same distance." The player
believed Spore's camera was broken in general.

## Findings

- The running game (Cell mode, Steam 3010a413, 40-dev) answered `coop.fov` with
  `steps=6`: the +18 degree wide lens was on. At the same native zoom distance a
  wider lens makes the creature look farther away, so both zoom endpoints look
  shifted outward. This matches the report exactly.
- The steps lasted the whole session: through stage changes, loads and editor
  visits. Nothing on screen showed that the lens was widened.
- The wheel handler accepted Ctrl from the message queue's key state
  (`MK_CONTROL` or `GetKeyState`). That state stays "down" when Ctrl is released
  in another window (alt-tab, overlay), so plain scrolling can widen the lens.
  Not proven to be how this player reached 6 steps; the fix removes the path.
- Negative results: the mouse hook changes nothing but Ctrl+Scroll;
  `SporeCoop.package` holds only UI images; `Data*/Config` differ from their
  backups only by `frameLimitMS`; `Preferences.prop` has no camera keys.

## Fix

- The lens returns to normal when the game leaves Creature or Adventure play
  (any mode except Creature, Adventure and the editors).
- Each change shows a local feed line ("Field of view +9 degrees (Ctrl+Scroll
  up to restore)", "Field of view: normal").
- Ctrl+Scroll needs `MK_CONTROL` and the physical key (`GetAsyncKeyState`).
- `begin_view` restores a projection whose `EndUpdate` never reached the hook.
- `coop.fov reset` / `coop.fov <0-6>` set the steps from the console.

## Verification

QA copy `out/release36/native` (Steam 3010a413), profile LineagesFov41QA (copy
of LineagesFoVZoomQA, save Samuth.spo, Creature), instance 8, 640x480 on the
secondary monitor, 5 FPS (`frame.limit 200`), muted. The old and new DLL were
swapped with `mod.reload coop` in the same session.

| Check | 40-dev (adcacb0f) | fix (04ad56ba) |
| --- | --- | --- |
| Posted WM_MOUSEWHEEL -240 with MK_CONTROL, Ctrl key up | steps 0 -> 2, lens 50.56 | steps stay 0, lens 44.56 |
| Reload the save (Creature -> menu -> Creature) | steps stay 2 | steps 3 -> 0, log "mode 02CCD1D2 resets" |
| `coop.fov 3` | (no argument support) | steps 3, lens 53.56, feed line "Field of view +9 degrees (Ctrl+Scroll up to restore)" (out/fov41-notice.png) |
| `coop.fov 9` | | usage message, rc 1 |

`exc`: 0 faults. Not covered: a real physical Ctrl+Scroll (injecting a global
Ctrl press would reach the player's foreground window), and an editor visit
keeping the steps. Player confirmation on the personal install is pending.

## History
- 2026-11-09: reported by the player; live state read through the console.
- 2026-11-09: fixed on fix/fov-stuck-wide-lens (04ad56ba); QA results above.
- 2026-11-09: released in 0.7.3.6-lineages41-dev.
