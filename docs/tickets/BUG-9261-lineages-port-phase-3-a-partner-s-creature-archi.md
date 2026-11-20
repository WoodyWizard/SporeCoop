---
id: BUG-9261
title: Lineages port, phase 3: a partner's creature archived with the host's planet, one key per creation
status: open
kind: feature
stage: Creature
reported: 2026-11-20
source: Lineages port (PR #196)
role: 
versions: 
fixed_in:
branch: feat/lineages-nests
commits:
tests: tools/tests/coop_home_nests.py (suite home_nests)
signature:
docs: docs/01-architecture.md#architecture-own-nests
related: BUG-9260, BUG-9262
---

## Report
Lineages port, phase 3 (PR #196; there BUG-9180 one key and the founder archive). A partner's own creature must live
in the host's save (its nest now, its village and nation later) and the planet must load while that partner is offline.

## Root cause
On main a player's creation was registered under 0x7C + fnv(owner network id, bundle id): the key changed when the
player rejoined under another id, and the owner itself played its My Creations key. The records existed only in the
package the host registered when the partner sent them: a cold host game had no model for that key.

## Fix
* `creations.one_key`: the key is 0x7C + fnv(0xC1, bundle id) on every game, the owner's included (own_key_here).
* `creations::archive_guest_model`: the bundle next to the planet, `<planet>.spo.duoguest-<KEY>`
  (founders::store_guest_model / load_guest_model / model_ids, from PR #196). Changes from PR #196: the archive also
  accepts the creation's pictures (png, BUG-0643: our bundles carry them, PR #196's decoder refused them); a bad
  archive is skipped with a warning and the planet still loads (PR #196 stopped the load); the stand-ins, rules,
  outfits and restore_world_designs are left out.
* A hook on `cGamePersistenceManager::LoadGameByName` registers the planet's archives before the load
  (`world::game_key_for_planet`, split out of current_game_key). `coop.creations archive [<player>]`.

## Verification
Windows, lane A (instances 1/2, Steam build), a new Creature game "Boheus" (the host plays the Maxis Burpee), the
guest chose its own My Creations creature 292846CB:
* both games: player 1's creature 7CA81684; after `coop.leave` + join: 7CA81684 again; with `coop.fix
  creations.one_key off` on both: 7CD0DCC4 (the salted key), on again: 7CA81684.
* `coop.creations archive 1`: boheus.spo.duoguest-7CA81684 (79873 bytes, 4 records). Guest left, host to the menu,
  host game killed and started cold, `coop.load Game0 Boheus`: log `archived creature 7CA81684 registered before the
  planet loads (BUG-9261)`, `coop.creations profile 7CA81684` a profile, no fault.
* With the nests (BUG-9262): the cold load showed the guest's nest with its 4 members drawn with the guest's model.
* Lab: tools/tests/coop_home_nests.py written for the Linux lab, not run there yet.
  no-fail-run: feature port, verified by hand on two instances (a new feature: the test fails on main, unknown commands).

## History
- 2026-11-20: opened, ported and verified on lane A (feat/lineages-nests).
