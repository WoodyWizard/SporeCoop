---
id: BUG-0488
title: Tribe: tools, members and nests sometimes stand elsewhere on the guest (more than 15 m / 3 m)
status: open
kind: desync
stage: Tribe
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in:
branch: fix/w43-realign
commits: cc62df1a
tests: realign_tribe_rejoin (coop_realign_planet.py --fixture tribe --case rejoin)
signature:
docs:
related: BUG-0523, BUG-0481, BUG-0514
---

## Report
Detector: `Tribe:unmatched:tribe_tool` 4% of Tribe comparisons, `unmatched:citizen` 1%, `unmatched:nest` 1%, `host_only:interactive_ornament` once (4 near the guest on the host only). Related to BUG-0481 (play_tribe: members not where the host has them). Class G8 (remote placement).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Analysis 2026-11-03 (fix/w41-tribe-world):
* Tools (`unmatched:tribe_tool`, the biggest share, 4%): observed in the code, nothing in the mod syncs tribe tools
  (no CreateTool on a guest, no tool table): each game makes the tools its tribes buy after the world was shared
  at its own spot. Split into BUG-0523 (to be done with BUG-0514's tool mapping on fix/w41-tribe-actions).
* Members (`unmatched:citizen`, "8 on the host, 6 on the guest, 2 host / 0 guest without a partner"): mirrors the
  guest had not spawned yet (BUG-0481's late mirrors), not members elsewhere.
* Nests (`unmatched:nest`, campaign_tribe#2 only): not analysed.
2026-11-04 (fix/w43-realign): the whole planet (coop.realign planet): after a guest's leave and rejoin in Tribe its game lacked 8 of the host's tools and had 1 of its own (far villages: the tools' snapshot covered 150 m around the guest, lab 20261104-212408). Nests: 49 on both games (tribe fixture), 113 objects on both in Creature after a rejoin (realign_creature_rejoin).

## Fix
Tools part (W2): the tribe tools' snapshot (worldobj.cpp, kind tribe_tool) covers the whole planet (3000 m, the 48 nearest a guest): a far village's tools are the host's too (made / removed as near ones). `coop.fix wobj.tools_planet off`: 150 m (0.7.6). The members' row (late mirrors, BUG-0481) is not changed.

## Verification
realign_tribe_rejoin: FAIL with `SPORE_FIX_OFF=wobj.tools_planet` out/lab/20261104-212803 (3 guest tools the host has not, 4 host tools missing), PASS out/lab/20261104-212748.

## History
- 2026-11-03: opened.
- 2026-11-03: analysed on fix/w41-tribe-world; tools split into BUG-0523, not fixed.
- 2026-11-04 (fix/w42-tribe-left): with BUG-0523's fix, SPORE_DETECTOR=20 over play_tribe, campaign_tribe and the Tribe tests (out/lab/20261104-102948, out/detector/20261104-1056.md, 62 Tribe comparisons): no `Tribe:unmatched:tribe_tool` row (was 4%), no `nest` row, `Tribe:unmatched:citizen` once (2%, "8 on the host, 7 on the guest, 1 host without a partner": a late mirror, BUG-0481's kind). Tools part done by BUG-0523; left open for the members' row.
- 2026-11-04 (fix/w43-realign): tools on the whole planet (wobj.tools_planet), runs above; open for the members' row.
