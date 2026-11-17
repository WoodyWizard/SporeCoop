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
fixed_in: 0.7.14.1-lineages64-dev
branch: feat/lineages-47
commits: 3083a39a, ae5c0ee5
tests: manual Windows QA with tools/qa_win/qa.py (TITAN, Dorger.spo and Samuth.spo, host 9 + guest 8)
signature:
docs: docs/re/creature.md#creature-summon-done
related: BUG-9129, BUG-9229, BUG-9233, BUG-0800, BUG-9246, BUG-9247, BUG-9248
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
* A save keeps the event as it is: a world autosaved while a partner's flock was out (switch off, TITAN Samuth 2026-11-16) loaded with the summon event active, 540-600 s past its time, its list naming 3 creatures that were in another herd; every Summon Flock in that world was refused ("none idle"), the host's own `coop.drama start 8` too, for good.
* The game never takes a creature out of the list when it changes herd (Creature_SetHerd), and the list keeps entries whose object was freed and given out again.
Inferred: the host's own flock ends by the posse code or distance (not checked).

## Fix
- creature.flock_release (drama.cpp tidy_summon_list): the summon herd's list keeps only the creatures still in that herd. At the hand-over (end_flock_event) every member is in the partner's pack's herd already, so the list empties; on every update of a summon past its time (one a save brought back) the same tidy runs. An empty list makes "done?" true: the manager ends the event at its next tick with the game's own end (vt+0x34), which finds nobody to take back. Creatures still in the herd (the host's own flock) stay listed and the game goes on with them as before.
- CORRECTION (2026-11-16, feat/lineages-47): the first version (commit 3083a39a, not released) let each member go as the update's end does (+0x135 = 0, +0xF90 = 0). +0x135 == 0 takes a creature out of the game's update: the flock stood frozen next to the partner (never in the host's update list, no proxy on the partner's screen, no fight), and the game's end then sent it back to the pool (BUG-9247). The let-go and the re-herd in guest_pack's tick that came with it are gone.
- `coop.fix creature.flock_release off`: 63-dev (the event runs on, later summons refused).
- Tests: `coop.drama summon` (the last summon event the game updated and its list), `coop.drama endsummon` (the tidy now).

## Verification
FAIL (63-dev, the fix not built yet; TITAN 2026-11-16 18:26-18:30, Dorger.spo): second and third guest summons "none idle"; the host's own `coop.drama start 8` refused too; summon active=1, its duration -184 -> -187 s; the herd list held 3 entries, one with +0x135 = 1.
FAIL again (switch off, TITAN Samuth.spo 21:25-21:35): the summon stayed active; after an autosave and a world reload every summon "none idle", `coop.drama summon`: active=1 duration=-539.6, 3 listed creatures in another herd.
PASS (the final fix, Samuth.spo 21:38-21:45): the stuck event of that save ended within a second of the fix loading ("602 s past its time: 3 creature(s) that left its herd out of its list, 0 left", active=0); summon 1: 5 creatures, event active=0 at once, the 5 in the host's update list (walking 1-5 m/s), on the partner's screen (8-11 proxies within 8 m), following a 70 m walk (12-42 m behind); summon 2 after the partner died: 3 creatures, event ended again.
no-fail-run: Windows QA by hand (no lab on this PC).
Not verified: the posse's summoned count +0x1C after the game's end; the host's own flock past its time (only the partner's and a saved one were seen).

## History
- 2026-11-16: opened during the owner-requested pack and abilities QA; cause found (the summon's "done?"); fixed (creature.flock_release, re-herd in guest_pack); PASS on TITAN.
- 2026-11-16 (later): the first fix froze the flock (+0x135); replaced by the list tidy, which also ends a stuck summon a save brought back; PASS on TITAN (Samuth.spo).
- 2026-11-17: released in 0.7.14.1-lineages64-dev.
