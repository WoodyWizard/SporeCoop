---
id: BUG-0350
title: Cell: a guest's bite on a host creature shows no health ring or hurt reaction on the guest, and the host sees no eating on the guest's cell
status: open
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (second batch: 11 zips, host and guest)
role: guest
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md (3, 10), docs/re/cell.md#cell-damage-gate
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
Not done. Candidates: (a) on the guest, after `send_hit`, show the ring the game would show
(call the health-ring function 0xE62350 for the mirror, RE needed for its arguments) and the hurt
animation, without the hp change; (b) send the eat / bite events of a player's cell with the state
and play the game's eat animation on the puppet. Cosmetic, but players read it as "the guest is
broken".

## Verification
Test to write: guest bites a host creature (BAF1AD40 cell with a weapon, coop_cell_spin.py style);
on the guest the mirror's health ring is shown (the field the ring sets, RE needed) within 0.5 s.
Must FAIL on main.

## History
- 2026-10-31: opened from the second batch of the 2026-10-31 report zips (host mark, code reading).
