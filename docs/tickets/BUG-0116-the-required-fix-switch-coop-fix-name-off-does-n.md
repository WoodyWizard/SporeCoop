---
id: BUG-0116
title: the required fix switch `coop.fix NAME off` does not exist, and several 0.7.2.x fixes have no switch
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#95
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-registry
commits: 4099ebc, dd3c506, ed70db7
tests: unit (tests/fixes_tests.cpp), fix_registry; twins via SPORE_FIX_OFF: creature_kills, creature_desync, cell_leave; alt_enter --without-fix
signature:
docs: docs/06-testing.md#testing-fix-registry, mods/coop/fixes.h
related: #95
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/95 (full text, discussion).

The project rules, the PR template, the fix-player-report skill and the ticket template all tell contributors to keep the old behaviour behind `coop.fix <name> off`, so the lab can later show a fix's test failing again. No such console command exists. Each fix has its own ad-hoc switch, if it has one at all:
- `coop.pool mirrorfix 0|1`
- `coop.npc cinedefer|strays|hatch|partners on|off`

Several fixes from 0.7.2.2-0.7.2.4 have no switch at all, so "the test fails without the fix" cannot be re-checked on a current build.

### Where
- The rule: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L90-L93
- PR template: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.github/PULL_REQUEST_TEMPLATE.md#L11
- Skill step 5: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.claude/skills/fix-player-report/SKILL.md#L23-L25
- Ticket template: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tickets.py#L74
- The switches that do exist:
  - https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2089-L2107
  - https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1786-L1788
- Command registration (no `coop.fix`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2713

### How to reproduce
**How to reproduce / check**

1. In the cheat console: `coop.fix mirrorfix off` gives an unknown command.
2. `grep -rn "coop.fix" .` matches only CLAUDE.md, the PR template, the skill and tickets.py.

### Expected
One documented switch command (`coop.fix` with no arguments listing all fixes and their state; `coop.fix <name> on|off`), on by default in release. Every fix where it is possible registers there. The docs and templates name commands that exist.

## Root cause
**What happens**

- The claim: CLAUDE.md says "A fix keeps the old behaviour behind a switch (`coop.fix <name> off`) where it can, so the lab can check later that the test still catches the bug." The PR template asks for "FAILS without the fix: ... (main @ <sha>, or `coop.fix <name> off`)".
- The reality: `grep -rn 'coop\.fix' mods sporekit` finds nothing; the string appears only in the four docs above. The existing switches each have their own syntax and are undocumented except in code comments and single tests.
- 0.7.2.x fixes and their switches:

  | Ticket | Fix | Switch |
  |---|---|---|
  | BUG-0001 | cell spin and bites | `coop.pool mirrorfix 0` |
  | BUG-0018 | migration crash | `coop.npc cinedefer off` |
  | BUG-0008 | strays | `coop.npc strays off` |
  | BUG-0014 | hatchling | `coop.npc hatch off` (unused by its test) |
  | BUG-0016 | NPCs target partners | `coop.npc partners off` |
  | BUG-0012 | Alt+Shift+Enter guard, gamefixes.cpp | **none** (its ticket says "FAIL with the hook off", which needs a rebuild) |
  | BUG-0006 | cell_leave forget | **none** |
  | BUG-0009 / BUG-0010 | creature_desync: stale proxies, species request | **none** |
  | BUG-0015 | play_death on mirrors | **none** |
  | BUG-0017 | nest_species | **none** |
  | BUG-0002 | cell meat | **none** |
  | BUG-0003 | join new game | **none** |

### Confidence
Confirmed by reading (grep over the whole tree). A second reviewer verified it.

## Fix
`mods/coop/fixes.h/.cpp`: one registry. A module registers each fix bool with one line next to its
definition, `COOP_FIX("<area>.<switch>", "BUG-NNNN", bool, "what it does")`; the registry only
points at the bools. `coop.fix` / `coop.fix list` (all switches: on/off, ticket, one line),
`coop.fix <name> on|off`, `coop.fix all on|off`. 102 switches registered (every switch of
`coop.pool fix`, `coop.npc <x>`, `coop.civ fix`, `coop.tribe fix`, `coop.res fix`, `coop.worldfix`,
`coop.corefix`, `coop.sessionfix`, `coop.framefix`, `coop.savestrip`, `coop.fleet`/`coop.space`
`<x>fix`, `coop.role fix`, `coop.give fix`); the old commands stay and act on the same bools.
SporeKit's fixes stay under `kit.fix` (another DLL).

New switches for 0.7.2.x fixes that had none: `game.adapter_key` (BUG-0012), `pool.forget`
(BUG-0006), `npc.stale_proxy` + `npc.world_despawn` (BUG-0009), `npc.play_death` (BUG-0015),
`creations.own_species` (BUG-0017). Not done (no single branch to switch, a redesign each):
BUG-0002 (cell meat), BUG-0003 (join the played planet), BUG-0010 (species request).

`SPORE_FIX_OFF=<name>,...` (tools/tests/twoinst.py) switches fixes off on both games right after
`twoinst.start()`, so any two-game test can run without a fix. Not done from the review's list:
a `tickets.py check` warning for fixed tickets without a switch (most older tickets name none).

## Verification
* `tests/fixes_tests.cpp` (make test): registry, command parsing, and a scan of mods/coop: every
  `COOP_FIX` well formed and unique, every bool named like a fix switch registered. FAIL before the
  registrations (72 fix bools without COOP_FIX), ok after (102 registrations).
* `coop_fix_registry.py` (suite `fix_registry`, lane 6): FAIL on main + the test (`unknown command:
  coop.fix`, run 20261028-025840, kept in the agent's scratch dir), PASS on the branch
  (out/lab/20261028-025806, out/lab/20261028-030127).
* The new switches bring their bug back (lane 6, SPORE_FIX_OFF / --without-fix, all FAIL):
  `coop_alt_enter.py --without-fix` (287 faults at 0x7777BE), `npc.play_death` creature_kills ("the
  guest's mirror is dead ... False"), `npc.stale_proxy,npc.world_despawn` creature_desync (22 living
  copies of old NPCs), `pool.forget` cell_leave (mirrors=144 kept, a guest fault).
  `creations.own_species` with SPORE_FIX_OFF does not reproduce BUG-0017: the host's creations reach
  the guest during the join, before the switch goes off (the species checks passed); the switch
  needs to be off before the join (not done).

### Related
#20, #51 (tests that pass without proving the fix).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #95.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-registry: coop.fix registry over every switch, new switches for BUG-0006/0009/0012/0015/0017, SPORE_FIX_OFF for tests.
- 2026-10-28: released in 0.7.3.0.
