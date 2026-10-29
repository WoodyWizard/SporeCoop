---
id: BUG-0078
title: the guest's bites skip the game's "won't attack the player" rule (ShouldNotAttack called with swapped arguments)
status: fixed
kind: bug
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#56
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-cell
commits: 0af9c83, 9413801, 82eeb38
tests: cell_guest_bite
signature:
docs: docs/re/cell.md#cell-damage-gate
related: #56
switch: pool.guestbite
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/56 (full text, discussion).

When a guest bites one of the host's NPCs, the host replays what DamageCell does around a bite: the species alert and the victim's reaction. It asks the game's ShouldNotAttack first, but with the arguments in the opposite order from DamageCell and without the hook that makes a partner count as the player. So the species flags "won't attack the player" never apply to the guest's bites. Such species are alerted and react to a guest's bite, but not to the host's. The guest's damage is also applied without the checks DamageCell makes before taking hp. Severity: wrong behaviour, minor.

### Where
- Host handler of `MSG_CELL_HIT`: [mods/coop/cellsync.cpp#L1237-L1260](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1237-L1260), the call at L1247:
  ```cpp
  if (uint8_t* pc = puppet_cell(from); pc && a_Alert && o_NoAttack && !o_NoAttack(pc, c)) {
  ```
- The hook that treats a partner's cell as the avatar: [mods/coop/cellsync.cpp#L725-L743](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L725-L743)
- DamageCell calls ShouldNotAttack(victim, attacker): [data/symbols/overrides_dc04aee5.sym#L29](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_dc04aee5.sym#L29)
- ShouldNotAttack applies the flags only when the second cell is the avatar: [docs/re/cell.md#L332](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L332), DamageCell checks: [docs/re/cell.md#L135](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L135)

### How to reproduce
1. Host and guest in the Cell stage, same pool. Find an NPC of a species with the "won't attack the player" flag (resource `+0x300` set). `coop.pool prey` on the host lists NPCs and how they rate each player.
2. Let the host bite it (`coop.pool kill c<key> hit` on the host): no alert, the species does not chase the host.
3. Let the guest bite the same kind (`coop.pool kill c<key> hit` on the guest): `hits_aggro` rises in the host's `coop.status`, and the bitten NPC's chase target `+0x22C` on the host becomes the guest's puppet index.

### Expected
A guest's bite on the host's NPC is judged exactly as the host's own bite: same ShouldNotAttack answer (with the partner counted as the player), same damage checks.

## Root cause
**What happens**

1. In the host's own bites, DamageCell (0xE7A7C0) calls `ShouldNotAttack(victim, attacker)`, and DamageCell also checks IsDamageable, `+0x17C` and `+0x111`. ShouldNotAttack(a, b) tests the `wontAttackPlayer(WhenSmall)` flags of a's species (resource `+0x300/+0x301`) only when b is the avatar (`G+0x411C`).
2. For a guest's bite, the host calls `o_NoAttack(pc, c)`: a = the guest's puppet, b = the NPC. b is never the avatar, so the flags of the NPC's species are never looked at. It also calls the original `o_NoAttack`, not the hook `h_NoAttack`, which would swap `G+0x411C` to the puppet so the game answers "as for the avatar".
3. For a species with "won't attack the player", the host's bite gets `ShouldNotAttack == true` inside DamageCell. The guest's bite gets `false`, so `AlertSpeciesOfAttack` and (for AI type 0x1006) `ReactToAttacker` run, and the NPC's kind turns on the guest.
4. The hp is then lowered unconditionally (`F<int>(c, kHp) - dmg`), without IsDamageable, ShouldNotAttack, `+0x17C` or `+0x111`. A cell the host's own bite could not damage can be damaged and killed by the guest.

### Confidence
PLAUSIBLE. The argument order is CONFIRMED by reading (DamageCell's call in the .sym entry, the mod's call). Not verified: what exactly DamageCell does when ShouldNotAttack returns true (skips the damage, the alert, or both), and which species of the fixtures carry the flag.

**Checked on 2026-10-28 (fix/w3-cell)**: confirmed in today's code (cellsync.cpp MSG_CELL_HIT host
handler: `o_NoAttack(pc, c)`, then hp lowered without a check). The GOG 2024 decompile of DamageCell
0xE7A7C0 settles the open point: when ShouldNotAttack(victim, attacker) is true, or the victim has
+0x17C or +0x111 set, or IsDamageable fails, DamageCell returns false with no hp taken, no alert and
no reaction (docs/re/cell.md#cell-damage-gate). In the test the host's own bite on such an NPC took
0 hp, the guest's took 3.

## Fix
The host's MSG_CELL_HIT handler now applies DamageCell's gate to a guest's bite (not to a mouth's
chunk, type 6, which the game's EatChunk does not gate either): the victim dead or at hp 0, +0x17C,
+0x111, or `h_NoAttack(victim, puppet)` (victim first, through the hook that lends the avatar index
to the puppet) drops the bite (`hits_spared=` in `coop.pool`). The alert and reaction ask the same
corrected question. Old behaviour: `coop.pool fix guestbite 0`. Test helper (host): `coop.pool
npcflag c<key> spare|invuln 0|1 | hp <n>`.

## Verification
Two-game test `tools/tests/coop_cell_guest_bite.py` (suite `cell_guest_bite`), the host's game hp of
the bitten NPC:
- FAIL with `SPORE_CELL_FIX=0`: out/lab/20261028-004547 ("spare: the guest's bite took nothing
  (54 -> 51)", "invuln: ... (51 -> 48)"; the host's own bites took 0 in both cases).
- PASS with the fix: out/lab/20261028-004704, out/lab/20261028-004804 (hits_spared=2).

### Related
- docs/re/cell.md section 9 (how the co-op path replays DamageCell's side effects): [docs/re/cell.md#L358-L371](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L358-L371)

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #56.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-cell (bug wave 3); test FAIL without the fix, PASS with it.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: pool.guestbite (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
