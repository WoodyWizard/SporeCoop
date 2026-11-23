---
id: BUG-1311
title: WP-D: the host's first DNA total after its editor or a load goes to every guest as the shared pool's
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w57-dna
commits: bbf4a90c b8821455 83a0781a 9699771d cd156c33
tests: tools/tests/coop_dna_own.py --case host_editor (dna_own_host_editor, twin dna_own_host_editor_off)
signature: guest log "coop: dna: 115 -> 65 cause=unknown" right before "dna: own 65 -> 115"
docs: docs/06-testing.md#testing-gates-own-dna
related: BUG-1291, BUG-1310, BUG-0829
---

## Report
Found while checking the gate failures of BUG-1310: in evo_screens_switch_lowfps (lab 20261119-102828) the guest's log
has "dna: 115 -> 65 cause=unknown" (a shared total: the host's own DNA) and in the same millisecond "dna: own 65 ->
115"; evo_bar_editors (20261119-110020) "dna: 20 -> 0 cause=unknown" then "own 0 -> 45", "own 45 -> 20". Both at the
moment the host's game left its creature editor (host log "stage: Editor -> Creature").

## Root cause
Observed: on_frame runs frame_send_totals before frame_local_update reads the game's mode into g_mode. In the first frame
after the host's editor the host has its avatar again (the totals are sent) while g_mode still says Editor, and
dna_total_for sent every guest the host's own DNA without the own flag (`g_mode != kCreature`). The guest applies a
shared total through AddEvolutionPoints of the difference (game::dna_add_raw); the next frame's own total sets its DNA
back (SetEvolutionPoints) but never lowers the bar. Inferred consequence: a guest that had spent more than the host
(its DNA under the host's) gets its bar raised by the difference: the guest's evolution bar runs ahead of the party's
for good (the BUG-0829 class); a guest above the host only flickers. The same edge is every transition into Creature
with the avatar ready in its first frame (a load of the host's world).

## Fix
dna_total_for decides "own total" by the game's mode now (game::mode_id()), not the frame's lagging g_mode. Switch
`creature.own_dna_edge` (off: g_mode, the stray total); `coop.dna own` prints edge= and strays= (totals of the host's
DNA sent to an own-wallet guest at such an edge, counted with the switch off).

## Verification
Switch `creature.own_dna_edge`; test dna_own_host_editor (coop_dna_own.py --case host_editor), twin
dna_own_host_editor_off.
- FAIL (switch off): 20261119-120634 dna_own_host_editor_off: the guest's log "dna: 30 -> 60 cause=unknown" (the
  host's DNA as a shared total), the monitor "bar: host 60, guest 90" and the sync-check "bar: host 60.0, guest 90.0"
  (the guest's bar 30 ahead of the party's until the next world share reloaded it); host `strays=1`.
- PASS: 20261119-120634 dna_own_host_editor (merged tree): no shared total on the guest, DNA 30 / 60, bars 60 / 60,
  `strays=0`.
- Note: the first form of the test (the host alone in its editor, the guest playing on) did not show it
  (20261119-113728 dna_own_host_editor_off passed with strays=1): a host editor counts as away on the guest after 4 s
  and its totals are dropped then; the test now has the guest leave its own editor first and the host right after.
  Earlier attempts 20261119-112341 (both FAIL: a gain of 150 grew the brain, whose caption held the host's editor).

## History
- 2026-11-19: opened.
- 2026-11-19: fix/w57-dna: fixed, lab runs above.
- 2026-11-23: released in 0.7.16.
