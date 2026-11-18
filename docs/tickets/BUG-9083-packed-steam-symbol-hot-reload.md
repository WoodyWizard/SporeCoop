---
id: BUG-9083
title: Symbol hot reload drops the packed Steam executable's database
status: open
kind: bug
stage: Framework
reported: 2026-11-07
source: Isolated Windows development loop
role: both
versions: SporeKit 0.3.6
fixed_in:
branch: feat/lineages-civ-combat
commits:
tests:
signature: sym.reload returns zero symbols and mod.reload coop refuses initialization
docs: docs/06-testing.md
related: BUG-9082
---

## Reproduction and workaround

Both isolated BuyH profiles on packed Steam 3010a413 had a loaded symbol
database. Following the documented sym.reload then mod.reload coop workflow
returned zero symbols and rejected mod initialization. Both still answered
core console commands and quit normally with zero faults.

symbols_load clears the database and selects a pending unpacked-code check
for this packed executable. This command does not finish that check. Existing
runtime hooks also make a new full text hash unsuitable as identity proof.
Cold launch from the same preserved checkpoint loaded the updated symbols.

Do not use sym.reload for this executable during playtesting. This tooling
issue is separate from campaign playability; use a cold checkpoint launch
when new symbols are needed. No production workaround or hash bypass added.
