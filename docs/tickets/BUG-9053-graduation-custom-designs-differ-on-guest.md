---
id: BUG-9053
title: Graduation city hall appears different on the guest
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-05
source: Native graduation full B local two-instance replay
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_civ_graduation_designs.py tools/tests/duo_tribe_graduation_cityhall.py
switch: creations.world_designs
signature: host custom three-block city hall guest stock skyscraper
docs: docs/re/civ.md
related: BUG-9046 BUG-0469
---

DuoQAGraduationFullB4/5 reach native Civilization with zero recorded faults,
but the host's custom three-block city hall looks like a stock skyscraper on
the guest. Inspect both native culture keys and actual building resources
before attributing this to missing creation transfer. The world creation scan
currently includes creature keys only; that is a hypothesis, not yet a cause.

Confirmed: native GetDesignKey returns host city hall 29160CE8, guest 068044ED;
host economic vehicle 29160CE8, guest zero. The guest resource manager lacks
both custom resources. The new regression fails on these native fields before
the patch (build/local/m2-civ-designs-control.log).

The outgoing save snapshot now includes a bounded, validated custom building/
vehicle archive. It is restored before native planet loading, including cold
loads, and does not edit the host's loaded save files. Only model/metadata/design
records from the two creation groups are accepted; missing parts/paints block
loading with a reason. Session cleanup removes registered overrides.

DuoQACivDesignsA4/5: build/local/m2-civ-designs-fixed-a.log PASS matching native
culture keys, both resources available, zero faults. The received Game3 contains
Clyth.spo.duo-designs (2498 bytes). Cold restart and full graduation replay are
initially remained qualification steps; separate civilizations remain BUG-9046.

Further verification:
- m2-civ-designs-cold-ready.log PASS in fresh processes, no hosting/joining.
  The first cold probe rejected native city index -1 (normal without a session);
  accepting that index fixed the assertion without changing the game state.
- m2-graduation-designs-full-a.log PASS full native editors and automatic stage
  transfer, matching newly created designs29161241, both founders retained,
  zero faults on both games. Profiles DuoQAGraduationDesignsA4/5.

Final candidate coverage: tribal-graduation-lifetime-final.log matches the
economic designs after a prolonged native editor. tribal-roles-offline-final.log
matches military designs after guest departure during editing and reconnect
after host advancement. The design regression now queries the actual native
vehicle role instead of assuming economic. Both runs exit cleanly, zero faults.
