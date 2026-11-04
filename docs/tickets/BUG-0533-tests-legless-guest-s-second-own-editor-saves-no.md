---
id: BUG-0533
title: tests: legless_guest's second own editor saves no new creature (the guest has 15 DNA after the first editor's charge)
status: fixed
kind: test
stage: Creature
reported: 2026-11-04
source: fix/w41-cell-left (BUG-0345 follow-up)
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-infra
commits: 46a858a3
tests: legless_guest
signature:
docs:
related: BUG-0278, BUG-0466, BUG-0345
---

## Report
`legless_guest` (coop_legless_guest.py --guest new) fails at "the guest changed its creature and the host shows the
new one": in the guest's second own editor (`coop.vote editor` on the guest, the game's own entry) the test drags a
mouth onto the creature, presses done and Save New, but My Creations gets no new creation ("back from the editor, no
new own creature (the avatar keeps ...)"). Seen on rel/0.7.5 lane 6 (out/lab/20261104-043804, in the rel worktree)
and on lane 4 with creations.guest_world_species on (out/lab/20261104-052627) AND off
(out/lab/20261104-053218): not the BUG-0345 rule. The last passing run is out/lab/20261102-163032 (main checkout),
before BUG-0466 (the own editor charges its parts).

## Root cause
Observed: after the first own editor the guest has 15 DNA ("own editor result: ... remaining DNA 65", then the
host's total); the screenshot of the second editor (out/lane4/shots/legless_guest_next_editor_new.png) shows no new
mouth on the body and 15 DNA in the editor. Inferred, not checked: since BUG-0466 the part the test adds costs more
DNA than the guest has, the edit does not happen, done leaves without a save. Also a sync-check gate
"n: 1 missing on the guest" in both lane 4 runs (not looked at).
Tried (not kept): `coop.dna add 200` on the host before the second editor: the host had 215, the guest still 15 when
its editor opened (the total had not reached it), and then the editor did not close at all ("the guest saves its
edit" failed too; out/lab/20261104-054104, out/lab/20261104-055343). To do: give the guest DNA its own game has
(wait until `coop.dna` on the guest shows it) before the editor, or a free part.

## Fix
Test fix (coop_legless_guest.py): before the guest's second own editor the host's game gains 40 DNA (`coop.dna add 40`, the party's total), the test waits until the guest's OWN game shows it (`coop.dna` on the guest: its editor reads its own total), Enters any brain-upgrade caption, then opens the editor. 40, not 200: the earlier try with +200 grew the brain and its cinematic's caption held the editor (out/lab/20261104-054104). Root cause confirmed (observed 2026-11-04): with the DNA the same mouth is placed and saved, so it was the BUG-0466 charge (15 DNA left, a mouth costs 25), not the mod. `--no-dna` keeps the old run. Not BUG-0345: the guest's new creature is the one it made in this world, creations_state is not needed here.

## Verification
FAIL (harness reason, without the gain): `SPORE_LANE=4 coop_legless_guest.py --guest new --no-dna` on fix/w42-infra, 2026-11-04 12:14: 'FAIL the guest changed its creature and the host shows the new one' (log kept in the worktree's out/legless_guest_nodna_20261104.log); also the lab runs of the report (fix/w41-cell-left out/lab/20261104-052627, -053218). PASS: lab legless_guest lane 4, out/lab/20261104-121027 (fix/w42-infra): 'DNA before the next editor: host 55.0, guest 55.0', 'next editor: guest 291538BF -> 291538C4, host shows 7C7F94A5 -> 7C86FA9A', RESULT OK, no gate. The sync-check gate 'n: 1 missing on the guest' of the report's runs did not show.

## History
- 2026-11-04: opened.
- 2026-11-04: fixed on fix/w42-infra (the test gives the guest DNA before its second editor).
- 2026-11-04: released in 0.7.6.
