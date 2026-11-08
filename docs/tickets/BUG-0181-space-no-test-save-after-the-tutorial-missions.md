---
id: BUG-0181
title: Space: no test save after the tutorial missions
status: fixed
kind: test
stage: Space
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in: unreleased
branch: fix/w46-g4b-coverage
commits: 67cc7dce
tests: space_free
signature:
docs: docs/re/space.md#space-tutorial-lock-runtime, docs/06-testing.md#testing-g4b-coverage
related: BUG-0199
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The `space` fixture starts at the first mission ("Fly to planet Vulpica", the wreck): the ship has
  no interstellar drive (tests add it with `coop.space tool add interstellardrive`), no
  terraforming tools, no colony besides home and no other empire has been met. `space_empire`
  (docs/06-testing.md 6.15) adds the drive, money, two colonies with spice and two contacts, made
  with test commands; it is still inside the tutorial (travel to stars that are not own stars is
  refused there), and has no raids, missions or trade. A save played through the tutorial would
  still be better.

## Root cause
The tutorial lock of galaxy travel is cMissionManager +0x18 mbMission201Finished (docs/re/space.md#space-tutorial-lock,
Ghidra): while it is 0 the travel check 0xFFC780 lets the ship go only to the home star and the Story201 target.
`space_empire` has it 0 on both games (`coop.space tutorial`: m201=0), so the red "Distance" refusal of
docs/06-testing.md 6.15 was this lock, not the drive range. No save played through the tutorial was needed: the
game's own release cheat `spaceCreate` sets the flag (runtime, docs/re/space.md#space-tutorial-lock-runtime). It also
removes the interstellar drive tool from the inventory (runtime finding), so the fixture adds it back.

## Fix
A fixture, no code change of the mod: `space_free` (out/fixtures/space_free.tgz, lane 3, 2026-11-08):

```
SPORE_LANE=3 python3 tools/tests/make_fixture.py space_empire space_free --cmd "coop.space tutorial" \
  --cmd "coop.space tools" --cmd "cheat spaceCreate" --cmd "wait.frames 60" \
  --cmd "coop.space tool add interstellardrive" --cmd "coop.space tool add interstellardrive_upgrade4" \
  --cmd "coop.space tool add energyupgrade5" --cmd "coop.space tool add placecolony" --cmd "coop.space tutorial" \
  --cmd "coop.space empire" --cmd "wait.frames 200" --cmd "key enter" --cmd "wait.frames 100" --cmd "coop.space tutorial"
```

Facts in tools/tests/fixtures.py (`coop.space tutorial` m201=1, drive=1, contacts, spice). The test command
`coop.space tutorial [done]` (spacecoop.cpp, fix/w46-g4-tests) prints the lock. Test `coop_space_free.py` (suite
`space_free`): both games zoom out to the galaxy view; the host clicks the nearest unowned star on its screen (a real
click on the star's projected point) and its ship flies there; the guest's game draws the host's ship tag at that star;
the guest clicks another unowned star in its own game and flies there; the host's game draws the guest there.
The guest's game has m201=1 too (its mission manager comes from the host's save): no co-op lock of its own.

## Verification
* Twin: `FIXTURE=space_empire` (the lock on): FAIL, lab 20261108-060412 lane 3: "the host's ship went to the unowned
  star Mitalis" and "the guest's ship went to ... Muellius" fail, both ships stay at home (m201=0 on both games).
* `space_free`: PASS 3 of 3, lab 20261108-060655 lane 3 (~55 s each).

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached: a save played through the Space tutorial is a fixture task (hours of play by a bot or by hand). Left open.
- 2026-11-08 (fix/w46-g4b-coverage): fixture space_free (spaceCreate ends the tutorial lock) and test space_free; twin space_empire fails (20261108-060412), 3 of 3 PASS (20261108-060655). Fixed.
