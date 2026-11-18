---
id: BUG-9198
title: Space rivalry and ally lists keyed by name: a rename or rejoin loses or duplicates them
status: fixed
kind: bug
stage: space
reported: 2026-11-14
source: roadmap
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 53e9de97
tests: tools/qa_win (two TITAN games by hand: rivals mode, war, leave, rejoin under another name)
signature:
docs: docs/re/space.md#space-identity-keys
related: BUG-9135 BUG-9189 BUG-9197
---

## Report
Roadmap phase 6 step 2 (decision 3N A): Space relations (rivals mode: war, alliance, proposals), the kept budgets and the ally mirrors were kept by player name. A partner who rejoined under another name was a stranger: the war or alliance was gone, its budget was not given back, and every other game showed a second mirror empire for it (the old one "away" until the session ended).

## Root cause
Observed (code and the FAIL run): space_rivalry.cpp g_rel_names, g_props_names and g_budget_name, and space_allies.cpp g_m, used `spaceext::name_of(id)` as the key; the persistent identity (BUG-9135) was known on the host only, so a guest could not key by it.

## Fix
identity.keys (on): the host sends its identity table (MSG_IDENTITY from the host: u8 count, u8 player + u64 identity each) to every guest when it changes and every 15 s; `identity::known(player)` works on every game, `identity::key(player)` gives "id:<identity>" (or "name:<name>" while unknown). Rivalry keys its relations, proposals and kept budgets by it; the allies key their mirrors by it (an entry made under the name before the table arrived moves to the identity key: "rekeyed" in `coop.space allies`). An older guest ignores the table (it only reads MSG_IDENTITY as a host). Off: the name, as before.

## Verification
Live, two TITAN QA games (2026-11-14, host DuoQAJiffSpace in Space, guest Fourth): rivals mode, the guest declared war on the host, left (coop.leave) and joined again as "Renamed". PASS (on): the host kept "1=war", the kept budget under the identity key, one mirror on each game (the guest rekeyed its name entry once). FAIL (`coop.fix identity.keys off` on both): "1=neutral" after the rejoin, two kept budgets (old and new name), two mirrors on the host (the old one away). no-fail-run: not a lab run (TITAN QA games, by hand).

## History
- 2026-11-14: opened.
- 2026-11-14: fixed (keys by identity, the host shares the table), live-tested with two games.
