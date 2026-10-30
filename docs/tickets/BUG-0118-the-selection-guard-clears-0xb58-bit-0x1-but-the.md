---
id: BUG-0118
title: the selection guard clears +0xB58 bit 0x1, but the GOG 2024 selection code tests bit 0x2 (contradiction, possible crash)
status: not-reproduced
kind: crash
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#97
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 
branch: fix/w1-creature
commits: b4dc4dd
tests: select
signature:
docs: docs/re/creature.md#creature-selection-gate
related: #97
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/97 (full text, discussion).

`h_SelectUI` guards `Simulator::CreatureSelectionUI` against the null `+0xE84` (archetype tuning) of puppets and NPC proxies. It does that by clearing `+0xB58 & 0x1` for the call, and the code calls that bit "pack member". The GOG 2024 disassembly note in the `.sym` file says the function tests **`al,2`** (mask 0x2) before it reads `(+0xE84)->+0x32C`. The docs give three different meanings for the bits involved. If the gate really is 0x2, the guard never covers the dangerous case, and selecting an NPC proxy after attacking it can still crash. CLAUDE.md "stop at a contradiction" applies: this needs a Ghidra check and a recorded CORRECTION before anything is built on it.

### Where
- The guard and its comment ("member of the player's pack (+0xB58 bit 1)", tests and clears mask 0x1): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L466-L489
- `data/symbols/overrides_dc04aee5.sym` L54 (GOG 2024): `Simulator::CreatureSelectionUI=0x00B6C970 ; verified port ...: start, plain ret; reads +0xB58 test al,2, then call 0xC0C300 (mov eax,[ecx+0xE84]) -> [eax+0x32C] at B6CA11 as described` https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_dc04aee5.sym#L54
- `data/symbols/overrides_922af7c7.sym` L113 (GOG 2016): "a pack member (+0xB58 bit 1) reads (+0xE84)->+0x32C". "bit 1" as a bit index is mask 0x2. https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_922af7c7.sym#L113
- docs/re/creature.md:
  - [L180](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/creature.md#L180) says 0x1 is "~ the alpha" and that the guard "treats the same bit as pack member".
  - [L354](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/creature.md#L354) says a pack member is `+0xB58 |= 0x100` (AddPosseMember 0xB23430).
  - [L66](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/creature.md#L66) says the stage event handler 0xD47FC0 needs `+0xB58 & 2` clear and sets it afterwards.
- `game::pack_member` uses 0x100: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.h#L110-L111
- The test checks only `& 1`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_select.py#L28-L31

The "pack member = 0x1" label is wrong in any case: the pack is 0x100, and 0x1 is the alpha by the docs.

### How to reproduce
**How to reproduce / check**

Ghidra checklist (GOG 2024 0xB6C970, GOG 2016 0xB6C930):
- [ ] Which immediate is tested on `[creature+0xB58]` in 0xB6C970, and which branch reaches `call 0xC0C300` / `[eax+0x32C]` (0xB6CA11)?
- [ ] In 0xD47FC0: where is the `or ..., 2` on +0xB58, and does it still run when `(+0xE84)->+0x32C == -1` (the blank object of `h_StageMsg`)?
- [ ] Writers of 0x1 (SocialPropose 0xDA43D0, "alpha") and of 0x100 (AddPosseMember 0xB23430): confirm the meanings.
- [ ] Runtime: on a guest, attack a proxy, read its `+0xB58` (`coop.npc` / `mem.read`), then click it.

### Expected
Selecting any puppet or proxy never reads a null `+0xE84`. The guard covers exactly the bit (or bits) the game tests, and the docs and `.sym` agree on what 0x1, 0x2 and 0x100 mean.

## Root cause
**The suspected gate does not exist.** GOG 2024 `CreatureSelectionUI` 0xB6C970, disassembled with
objdump on 2026-10-27:

```
b6c9ba  call 0xc0b8d0        ; adult (+0xB34 == 1)
b6c9c3  mov eax,[esi+0xb58]
b6c9c9  test al,0x2
b6c9cb  jne 0xb6ca37         ; mask 0x2 set: skip the read
b6c9cd  test al,0x1
b6c9cf  je 0xb6ca37          ; mask 0x1 clear: skip the read
b6c9d1  ... vt 0xD0, vt 0xD4 both false
b6ca0c  call 0xc0c300        ; mov eax,[ecx+0xE84]
b6ca11  mov ecx,[eax+0x32c]  ; the crash site
```

The read needs mask 0x1 **set** and mask 0x2 **clear**. A set 0x2 is an early-out, so the guard in
`h_SelectUI` (hides 0x1 when +0xE84 is null) covers exactly the path that reads +0xE84. The rest of
the function and its direct callees do not touch +0xE84. The step-1 scenario of the report (the
stage handler sets 0x2 after an attack, then a click crashes) cannot happen this way: with 0x2 set
the game skips the read.

What was wrong is the wording. The `.sym` note "test al,2, then call 0xC0C300" read like a gate on
0x2, and the code comment called mask 0x1 "pack member" (the pack is 0x100).

## Fix
No code change needed. Corrected the docs (commit b4dc4dd): the CORRECTION and the evidence are in
docs/re/creature.md#creature-selection-gate, the `.sym` note of 0xB6C970 in
`overrides_dc04aee5.sym` now gives the real condition, and the coop.cpp comment of `h_SelectUI` names
the mask instead of "pack member". The frozen GOG 2016 file is unchanged (its "bit 1" means mask 0x1).

## Verification
Static only (the disassembly above). The existing `select` test (it clicks the partner's puppet
with mask 0x1 set and +0xE84 null and checks that the guard ran) passed in the smoke run on lane 4 (out/lab/20261027-190036).
No new test: there is no failing case to reproduce.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #97.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: checked on fix/w1-creature: 0x2 is the early-out, 0x1 the gate; the guard is right. Docs and `.sym` corrected; closed as not-reproduced.
- 2026-10-30: fix/w13-creature: settled again statically on the GOG 2024 exe (objdump 0xB6C9C3..0xB6CA11: `test al,2; jne skip; test al,1; je skip` before `call 0xC0C300` / `[eax+0x32C]`), and the guard in coop.cpp `h_SelectUI` still hides mask 0x1 when +0xE84 is null. No contradiction left; stays not-reproduced (docs/re/creature.md#creature-proxy-guest-side).
