---
id: BUG-9163
title: Creature: the party migrates together: any player's migration warns everyone, 10 s later everyone edits (decision 2G)
status: open
kind: feature
stage: creature
reported: 2026-11-13
source: owner
role: both
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + guest 2026-11-13: coop.evolve migrate)
signature:
docs: docs/re/creature.md#creature-migrate-together
related: BUG-9160 BUG-9162 BUG-0508 BUG-0019
switch: creature.migrate_together
---

## Report
Owner decision 2G (2026-11-12): anyone starting the native migration warns every player in the world, 10 s later all go into the editor for final touches (downed or dead players too) and migrate together. With personal editors (BUG-9160) a guest's own evolution could start a migration on its own game only.

## Root cause
Design. The game starts a migration when an editor returns with the stage total above the next threshold (CreatureGame_AfterEditor: cPlayer property 0x514A219 = 1, CRG_SpeciesMigrate; docs/re/creature.md 5). The bar is the party's (BUG-9162), so every game that edits then migrates. The host's migration already sends every guest the world (BUG-0508's evolve-in-place check: "a migration started").

## Fix
creature.migrate_together (on). Each game watches its own migration state (0 -> 1); then a guest tells the host (MSG_MIGRATE 86: kind 1, the player, the world), the host tells every other guest (or every guest when it started it); each told player gets "<name>'s species is migrating to a new nest. Everyone migrates together: your editor opens in 10 s for final touches." and 10 s later its own evolution editor (the creature stage's request, coop.cpp submit_evolution_request, factored out of coop.vote evolve), with the cocoon for the others. Down, dead or busy: tried each second for 30 s more. The host's editor ends in the host's migration, which sends the world. `coop.evolve migrate` (tests: as if this game started one), `coop.evolve migration`.

## Verification
no-fail-run: a feature. Live 2026-11-13, TITAN host + guest: `coop.evolve migrate` on the guest: the host logged the warning, 10 s later "own editor for the final touches -> 1" and was in its editor; the guest showed the host in the cocoon; 0 faults. Not seen: a migration started for real by a guest's evolution (the bar past the threshold; a test jump of +250 DNA crossed a brain level, not the migration), the world share after the host's migration in this flow.

## History
- 2026-11-13: opened.
- 2026-11-13: live with a real migration (the bar 701 crossed a threshold above 601; TITAN host + 2 TITAN guests + the MSI as a guest over Tailscale (relayed), 2026-11-13, all players with a creature of their own): (1) a guest's
  own migration was never seen: its state goes 0 -> 1 on the return from the editor and the watch had been reset while
  the game was in the editor; now the state seen before the editor is kept through it (a load starts over:
  game::stage_epoch). (2) Then the MSI's migration warned everyone, 10 s later the host and both TITAN guests were in
  their editors; but each of those "final touches" editors started that player's own migration and warned everyone again
  (a cascade of rounds). Now one migration is one round: a warned game ignores further warnings for 300 s and a
  migration from its warned editor tells nobody. Seen after that: every game at state 2 with the same new nest
  (185 -465 -69, the host species' new nest); the host claimed it (CRG_ClaimNest), the world went to every guest, the
  players' own nests were made 60 m from the new home (BUG-9166, BUG-9167, BUG-9168 found and fixed on the way) and
  each guest moved into its own nest.

