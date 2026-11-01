---
id: BUG-0350
title: Cell: a guest's bite on a host creature shows no health ring or hurt reaction on the guest, and the host sees no eating on the guest's cell
status: fixed
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (second batch: 11 zips, host and guest)
role: guest
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w22-reports2
commits: 6f18d53, 8e58c97
tests: tools/tests/coop_cell_bite_ring.py (suite cell_bite_ring)
signature:
docs: docs/re/cell.md#cell-guest-bite-ring, docs/06-testing.md#testing-cell-reload-ring
related: BUG-0078, BUG-0240, BUG-0001
---

## Report
Host mark (19:19:34 host time, Cell, guest on Steam): "Guest doesnt have eating animations and
doesnt see enemies HP".

## Root cause
Observed (code, `cellsync.cpp`):
* On the guest the DamageCell hook returns `false` for a victim that is a mirror of a host cell
  after sending `MSG_CELL_HIT`, without calling the game's DamageCell. DamageCell 0xE7A7C0 is what
  shows the health ring (0xE62350, when the avatar is involved) and plays the victim's hurt
  animation (docs/re/cell.md table 3). So a guest's weapon bite (Bite 0xE7B0A0, types 4 and 5) on a
  host creature shows no health ring and no hurt reaction on the guest; the host applies the damage
  (host log `bite: from 1 c... dmg=2 hp 6 -> 4`) and the hp arrives with the state. Only a mouth's
  chunk (EatChunk, `chunk()`) still runs the original and shows the ring. The guest's bites in this
  session were all `type=4/5` (guest log `bite: sent ... type=4`).
* The host's picture of the guest is a puppet moved by the state; the mod plays no eat or bite
  animation on puppets (no animation call in cellsync.cpp besides the death replay), so the host
  sees the guest's food vanish without the eating motion.
Inferred: the mark's "doesn't see enemies HP" is the first point (guest side), "no eating
animations" the second (host side, or the host relaying what the guest said). Not seen in a game.
The mark came 30 s after a reload in which the guest was 125-133 m from the host with nothing on
screen (BUG-0352), so the player described earlier minutes.

## Fix
`cell.bite_ring` (cellsync.cpp `bite_ring`, guest): after sending the hit, the guest calls the game's
ShowHealthRollover (0xE62340) on the mirror itself, under DamageCell's own conditions (damage > 0,
GFX, hp > 0, not dead or dying, +0x17C and +0x111 clear, ShouldNotAttack false). The hp change and
the creature's reaction stay the host's. `coop.fix cell.bite_ring off`: no ring (0.7.3.1).
Not done: (a) the hurt animation on the mirror: DamageCell plays it and schedules event 2, whose end
runs the after-hurt AI reaction, not wanted on a passive mirror; (b) eat / bite animations of a
partner's puppet on the other machine: needs the cell eat animation ids and a message per meal (RE
and protocol work, not cheap). Both stay open here as a smaller follow-up.

## Verification
`coop_cell_bite_ring.py` (cell fixture, lane 3). The ring is the game's state: the cell UI's
rollover pool entry of that cell (`ring=`); the host's own bite is the control (ring 0.50 in the
host's game).
* FAIL with `SPORE_FIX_OFF=cell.bite_ring`: out/lab/20261031-130728 (ring=-1 on all 3 guest bites
  while the host took the hp), also 20261031-125228 and 20261031-130423.
* PASS: out/lab/20261031-130836 (2 runs, ring 0.50 on each of 3 bites, 0.4-0.5 0.1 s later), also
  20261031-130238.
Found on the way: right after a teleport to a fleeing creature its mirror often has no GFX on the
guest (not drawn there yet), so the game shows no ring (with or without the fix); the test waits for
the GFX.
Related tests PASS on lane 3: out/lab/20261031-132041 (cell_editor_invuln, cell_guest_bite, cell_rescue,
cell_respawn_swap, creations_cell, cell); `make test` PASS.

## History
- 2026-10-31: opened from the second batch of the 2026-10-31 report zips (host mark, code reading).
- 2026-10-31: the ring fixed on fix/w22-reports2 (cell.bite_ring); hurt and eat animations not done.
- 2026-11-01: released in 0.7.3.2.
