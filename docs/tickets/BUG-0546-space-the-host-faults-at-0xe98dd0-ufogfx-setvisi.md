---
id: BUG-0546
title: Space: the host faults at 0xE98DD0 (UfoGfx_SetVisible, null model) after a jump to an NPC empire's system in space_diplomacy, lane 5 only
status: open
kind: bug
stage: Space
reported: 2026-11-04
source: space_diplomacy on lane 5, fix/w42-space-life regression runs
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00E98DD0 SporeCoop::UfoGfx_SetVisible+0x40 data 0x4 (host, main thread)
docs:
related: BUG-0323, BUG-0469
---

## Report
space_diplomacy on lane 5 (GOG 2024): the host jumps to the NPC empire's star 004A0014 ("Plushio The 3rd
Empire"), about 1.8 s later its game faults once at 0x00E98DD0 (`UfoGfx_SetVisible+0x40`, data 0x4, the null
model of BUG-0323) and the lab fails the test on the fault: out/lab/20261104-110322, out/lab/20261104-111725 and
out/lab/20261104-111922 (the last with space.save_strip, space.keep_colonies and kit.longout all off). The same test
passes on lane 4 (out/lab/20261104-113833). Not the mod's ships (Bob's puppet was hidden, the host shows no
mirrors): inferred, a ship of that empire whose model lane 5's host profile lacks (BUG-0323 was lane 8 only, on the
guest). Not analysed further (not in S2's tickets). Reproduce: `lab.py run space_diplomacy --lane 5`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
