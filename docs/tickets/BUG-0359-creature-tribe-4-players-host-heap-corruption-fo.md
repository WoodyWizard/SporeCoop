---
id: BUG-0359
title: Creature -> Tribe, 4 players: host heap corruption found freeing a plant's spatial-grid node (GeneralAllocator_FreeInternal+0x64 from 0x702C44)
status: open
kind: crash
stage: Creature
reported: 2026-10-31
source: lab 20261031-185843 campaign#3
role: host
versions: 0.7.3.1
fixed_in:
branch: fix/w27-heap
commits:
tests: campaign, campaign_ct, campaign_tribe
signature: dc04aee5:GeneralAllocator_FreeInternal+0x64 < delete_ < 0x00702C44 (HierGrid unlink) < 0x00702E88 < 0x00B0818E (flora HandleMessage)
docs: docs/re/tribe.md#tribe-flora-grid-free
related: BUG-0308, BUG-0348, BUG-0170, BUG-0276
---

## Report
Lab 20261031-185843 campaign#3 (main 55da78b, lane 7, 4 players, `coop_campaign.py --minutes 8`
cell -> space, seed 132463235), the HOST (Alice). The host played Cell (19:59-20:09 log time),
Creature (with Bob's leave and rejoin, two editors), then the creature advance to Tribe. In the
creature advance: a world share started at 20:19:59.9 (two saves, "5 objects of ours are kept out
of it"), the game entered Tribe at 20:20:02.632 during that share ("the game went from Creature to
Tribe during the save ...: saving again", no new save posted: the retry waits 2 s), the share was
queued again at 02.750, and at 20:20:02.886 (0.25 s into Tribe):

    exception 0xC0000005 at 0x009272C4 GeneralAllocator_FreeInternal+0x64 (data 0x586472AC)
      eax=00000049 ebx=02E1BD68 ecx=3F028F5C edx=02E1C250 esi=1961E350 edi=3F028F58
      calls: 00927979 00F47234 00702C44 00702E88 00B0818E 00B04A8A 00B04A97 00B085B2 00B78155 00B78764

Nothing more was logged; the host's console stopped answering and the campaign saw it FROZE (no
frames, no watchdog stack in the log: the fault left the allocator's lock held, inferred). The
guests were fine. The machine was heavily loaded at that time (another agent ran 4 lanes).

## Root cause
Observed (Ghidra, GOG 2024; docs/re/tribe.md#tribe-flora-grid-free):
* The block being freed is a 16-byte list node of a spatial grid: 0x702C3F is
  `GeneralAllocator::delete_` in HierGrid unlink 0x00702B80, called from HierGrid remove 0x00702E30
  (0x702E83), called from the flora object's tree removal 0x00B07F00 (0xB08189; flora +0x16500 is the
  grid, a tree record's +0x78 its handle), called from the flora object's HandleMessage 0x00B081C0
  (0xB085AD) for message 0x02E9977E / 0x025630B7 / 0x0477C00D. That is the flora (cTreeData patches,
  [0x167FB14]) rebuilding trees as Tribe begins. 0xB04A8A / 0xB04A97 / 0xB78155 / 0xB78764 are stale
  return addresses of the stack scan (cObstacleManager's HandleMessage 0xB78500 handles the same
  messages, "Simulator/cObstacleManager" vtable 0x01465068).
* The node's chunk header (0x1961E354, the word before the node 0x1961E358) held **0x3F028F5C =
  0.51f** instead of a size: FreeInternal took 0x3F028F58 as the size and read the next header 1 GB
  further (0x586472AC). A float written over a heap header: a write one or more dwords past the end of
  the block before it, or a write through a stale pointer into memory that was reused. Not a double
  free (a freed chunk does not hold a float).
* The same site (FreeInternal+0x64) is only the generic "corrupt header" place: BUG-0308 / BUG-0348
  (a guest freeing its Cell pools when leaving Cell, header 0x41FAECC0 = 31.37f from the overflowed
  cell broadphase endpoint arrays) is another stage, another role and another block.

Inferred, not shown: who wrote 0.51f. Not today's Cell switches (cell.gfx_full, cell.spare,
cell.bare_still, cell.particles, cell.sap_pending, cell.sap_sweep):
* the host had left Cell 11 min earlier (20:09) through two editors and a creature world; the freed
  node belongs to a tree of the creature planet (allocated when the planet's trees were planted, in
  Creature, after Cell's pools were gone);
* every function those switches hook is called only by Cell code (Ghidra raw call scans, GOG 2024:
  CreateSoftBody 0xE6D8F0 only from the soft-body LOD 0xE78C00; ParticleAlloc 0xE4DBA0 / ParticleFree
  0xE4EC10 only from 0xE51A20 / 0xE5CB60 / 0xE66010; UpdatePairs 0xBBB450 only through 0xBBBF60 from
  Cell::SimUpdate; AddObject 0xBBB290 only through 0xBBBF40 from CreateCellObject), and our own ticks
  (spare_tick, the guest / host ticks) run only `in_cell()`;
* the host's log has none of their warnings in Cell (no full graphics pool, no full particle pool /
  spare, no broadphase sweep over 1024, no pending-list bound).
Today's other change at the Tribe arrival, tribe.place_chiefs / spread_chiefs (BUG-0340, host), did
not act before the fault: place_chiefs waits 3 s after the arrival, spread_chiefs needs two chiefs
stacked for 1 s, and both log when they move a chief (no such line; the fault came 0.25 s in).
Pre-existing and rare: the Creature -> Tribe change during a share save happens in nearly every
campaign (77 game logs since 2026-10-29 have "went from Creature to Tribe during the save", 72
logged Creature -> Tribe changes in the lab history); this is the only fault within 20 s of one.

## Fix
None: the writer of the float is not known, so there is nothing to switch. Next step: a heap walk
tripwire (PPMalloc chunk sizes inside each core block, or the flora grid's nodes alone) run once a
second in a stress campaign would date the damage to an event (a share save, an editor, a rejoin).

## Verification
Not reproduced. Stress on main 3606699 + the suite entry (fix/w27-heap 0fe73b8), lanes 3 and 5,
all fixes on (the release configuration): out/lab/20261031-204524-fix-w27-heap, 8 tests in 41 min,
all passed: 4x campaign_ct (Cell -> Creature -> Tribe, 4 players, 2 min per stage) and 4x
campaign_tribe (Creature -> Tribe). Every one of the 8 hosts took the same path (the game entered
Tribe during a share save: "went from Creature to Tribe during the save" x3 in each host log); no
exception in any of the 32 game logs. Runs with `SPORE_FIX_OFF=` the Cell switches were not made:
with 0 faults in 8 runs and 1 in ~25 before, they could not tell the two apart.
no-fail-run: not reproducible (1 fault in 72 logged Creature -> Tribe changes; 0 in 8 stress runs).

## History
- 2026-10-31: opened.
- 2026-10-31 (fix/w27-heap): analysed (Ghidra: the flora's tree grid node, header overwritten by
  0.51f); not today's Cell switches (Cell-only callers, host left Cell 11 min before, no warnings);
  stress out/lab/20261031-204524-fix-w27-heap 8/8 ok. Stays open, no fix.
