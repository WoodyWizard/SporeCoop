---
id: BUG-1083
title: Tribe: tribe_gameover_partner flakes: the game-over caption never comes, the host's knocked-out chief has health again (168) when the guest's members are down
status: fixed
kind: test
stage: Tribe
reported: 2026-11-13
source: fix/w49-hutwait, lab 20261113-140322 lane 6
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-chief
commits:
tests: tribe_gameover_partner
signature:
docs: docs/re/tribe.md#tribe-gameover-list, docs/06-testing.md#testing-gameover-partner-timing
related: BUG-0922, BUG-0960, BUG-1052
---

## Report
Seen on fix/w49-hutwait (BUG-1081, a change that does not touch the knock-out path), lab 20261113-140322 (lane 6,
cold start): phase 1 knocked the host's members out (chief #1 hp 0, dead=1 for 80 s, no caption: ok). In phase 2
the test's "knocked out on the host" list held only #1, and for the whole 70 s wait the chief read `hp=168.0 dead=0`:
the caption never came (`FAIL the game over came on the host`). The rerun on the same build passed
(20261113-141231). Earlier failures of this test: 20261112-092315, 20261112-093040 (fix/w48-tribe-mirrors, before
BUG-0922/0960). Inferred: the host's knocked-out chief got health back (BUG-0922: a knocked-out chief may stand up
again) before the guest's members were down, or the guest's members (#2, #8) were no longer in the host's list.
Reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_gameover_partner --lane N --retry 0` (about 1 in 5).

## Root cause
A test artefact (its own blows) on top of the game's own game-over rule; no mod code gives the chief its health back.
* Observed (Ghidra, GOG 2024): `cCreatureCitizen::TakeDamage` 0xC26940 posts the game over (message 0x5C80207) at a
  player member's fall only when the tribe's up list (cTribe +0x354, `SporeCoop::cTribe_UpCount` 0xC8F500) holds fewer
  than 2 members. A fallen member leaves that list at its next behaviour tick (death node 0xDA78C0); a knocked-out chief
  leaves it in its knockout node 0xDB1A30 and comes back with full health (SetHealth(max), 0xDACCD0) 91-97 s after its
  fall: the hp 168 is the game's own stand-up (not the chief rescue, the HP sync or realign: the host's log has no set
  of the chief's health, and the rescue of BUG-1052 only leaves 30 % at a blow, seen as `saved` 1-2 s before the fall).
* Observed (lab, the test printing the up list and the host's `busy`): with the chief knocked out and the host's own
  members dead the list held the two guest members. The old test's phase 2 hit both with two console calls back to
  back; when both killing blows landed before the first fallen member's behaviour tick, both falls saw 2 and no game
  over came at all (`busy -`, no input lock, the chief still down for 8 s after the falls): 20261113-140322 (the
  report), 20261113-142822 #3. Forced into one frame (`coop.tribe pincer 2,8 ...`, twin): no game over in 3 of 3
  (20261113-144926 #1 / #2, 20261113-151255); one member at a time: the input lock at the last fall, the caption about
  10 s later in every run.
* Inferred: the report's "chief has health again" was the consequence (it stood up 95 s after its fall, 9 s after the
  falls that brought no game over), not the cause. The rule is the game's own (members falling in the same tick never
  bring a game over; with a knocked-out chief the tribe plays on once it stands up), so not a co-op bug; an epic's area
  blow could do the same in a play (not seen).

## Fix
Test only (no game change). `coop_tribe_gameover_partner.py`: times counted from the chief's fall; 60 s without a
caption while the guest's members live; then the guest's members killed one at a time, the last killing blow 84 s
after the chief's fall (the chief still down, up 6-12 s later as BUG-0922's members-rule twin needs); prints the up
list and `busy`. Twin `GO_PARTNER_SAME_FRAME=1`: the last two killing blows in one call. `coop.tribe pincer` takes a
comma list of victims (one frame).

## Verification
* FAIL (twin, the last blows in one frame): out/lab/20261113-144926 (#1, #2), out/lab/20261113-151255.
* FAIL (natural, the old blows): out/lab/20261113-140322 (report), out/lab/20261113-142822 #3.
* PASS: out/lab/20261113-144931 (#1-#3, intermediate timing), out/lab/20261113-150427 (#1, #2), FINAL_PASS.
* BUG-0922's twin still fails with the new timing (`PLAY_TRIBE_OLD_GAMEOVER=1`): out/lab/20261113-150933.

## History
- 2026-11-13: opened.
- 2026-11-13: root cause found (the game's up list, two falls in one tick), the test fixed on fix/w49-chief.
