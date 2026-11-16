---
id: BUG-9245
title: Creature: after a partner's Summon Flock no Summon Flock starts again (the event never ends)
status: fixed
kind: bug
stage: creature
reported: 2026-11-16
source: QA TITAN 2026-11-16
role: host
versions: 0.7.14.1-lineages63-dev
fixed_in: unreleased
branch: feat/lineages-47
commits:
tests: manual Windows QA (TITAN, Dorger.spo copy, host 9 + guest 8: coop.prules roar 0x33, coop.drama, coop.gpack)
signature:
docs: docs/re/creature.md#creature-summon-done
related: BUG-9129, BUG-9229, BUG-9233, BUG-0800, BUG-9246
---

## Report
Owner question 2026-11-16 ("Have you tested that connected players can form packs and use abilities like Summon?"). QA on TITAN (63-dev, host instance 9 + guest instance 8, the Dorger.spo copy): the guest's first Summon Flock worked (host: "the flock summoned 3 for player 1"); every later one, the guest's and the host's own (`coop.drama start 8`), did nothing: "drama: summon event for a partner's creature: none idle", `coop.drama`: summon active=1 for 200+ s.

## Root cause
Observed (runtime, the Steam code read from the running game, Steam = .sym - 0x110000):
* The Drama Manager tick (Steam 0xC3E450) only counts an active event's duration +0x2C down (it read -187 s and kept falling); it ends an event (+0xC = 0, vt+0x34) only for the +0x30 avatar condition, the time-of-day condition or the event's vt+0x3C "done?".
* The summon event's "done?" (vt+0x3C, Steam 0xC3D2F0, vtable 0x0136B398) is true only when its herd (prop 0x609F2F8 -> vt+0xC(0x52AA6117)) is missing, its list +0x40..+0x44 is empty, or every creature in that list has +0x135 == 0.
* Its update (vt+0x40, Steam 0xC412A0) sets +0x135 = 0 (and +0xF90 = 0) only at its end and only for a member over 100 m from the leader (50 m when spatial vt 0x48 is false).
* A partner's flock is handed to the partner's pack (BUG-9129) and kept at the partner's side, so no member was ever let go: BUG-9233's "ends now" (duration 0) never ended the event, and the one summon event stayed active for the session.
* The list also kept creatures that had died: after the flock drowned at the partner's underwater nest (BUG-9246) one entry was a newborn of the host's species at a reused address (+0x135 = 1), so it could not end even by distance.
Inferred: the host's own flock ends by the posse code or distance (not checked).

## Fix
- creature.flock_release (drama.cpp end_flock_event): at the hand-over each member in the summon herd's list is let go as the update's end does (+0x135 = 0, +0xF90 = 0); the manager ends the event at its next tick (the game's own end, vt+0x34).
- The game's end takes the members out of every herd (runtime: herd 0, they stood still): guest_pack's tick puts a member without a herd back into the partner's puppet herd (guest_pack.cpp, under creature.guest_pack's herd fix).
- `coop.fix creature.flock_release off`: 63-dev (the event runs on, later summons refused).

## Verification
FAIL (63-dev, the fix not built yet; TITAN 2026-11-16 18:26-18:30): second and third guest summons "none idle"; the host's own `coop.drama start 8` refused too; summon active=1, its duration -184 -> -187 s; the herd list held 3 entries, one with +0x135 = 1.
PASS (the fix, after a world reload, 18:40-18:42): summon 1: "5 let go", `coop.drama` summon active=0 at once, the 5 members back in the guest's puppet herd at 3.6 m (herd_moves 5 -> 10); summon 2 two minutes later: "3 let go", 3 more creatures (summons=2, summoned=8).
no-fail-run: Windows QA by hand (no lab on this PC); the switch-off run was not repeated after the fix (the 63-dev run above is the failing one).
Not verified: the flock following a long walk and attacking for the partner (the guest died to the sea monster at its underwater nest, BUG-9246, before those checks); the posse's summoned count +0x1C after the game's end. Seen once, not explained: after summon 2 two of the 5 listed members were in other herds (one in the host's nest herd, 18 hp).

## History
- 2026-11-16: opened during the owner-requested pack and abilities QA; cause found (the summon's "done?"); fixed (creature.flock_release, re-herd in guest_pack); PASS on TITAN.
