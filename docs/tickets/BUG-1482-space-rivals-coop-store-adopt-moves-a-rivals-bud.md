---
id: BUG-1482
title: Space Rivals: `coop.store adopt` moves a Rivals budget that nobody then sends to the player (SPC-3)
status: open
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w77-space
commits: 48cdcb66, b8cd7f35, 37d9240d
tests: space_rival_adopt, space_rival_adopt_off, space_tests (make test)
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-1107, BUG-1152, BUG-1029
switch: space.rival_adopt
---

## Report
External static audit of 0.7.15.5, item SPC-3 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code (rel/0.7.17 4daf12d1), confidence high; the audit's reading confirmed:
* `coop.store adopt <player>` (store.cpp) calls every feature's adopt callback; space_rivalry.cpp's only did
  `g_store.rekey(from, to)`: the stored budget moved to the new key and nothing else.
* The adopted guest is in the session and its budget is known (`g_win.known`: it joined as a newcomer, K_SET have 2,
  budget 0), so it never asks again (K_ASK) and would drop a K_SET (`... || g_win.known) break;`).
* Its tick reports its money (K_MONEY, every <= 3 s) and the host's K_MONEY handler `budget_put(kept_key(origin), v)`
  replaced the adopted budget by the guest's 0 within seconds: the budget was lost for good.
* The rel_keys part of the audit item is harmless (a self pair is never read), as the verify file says.

## Fix
* The host, after the rekey, sends the adopted budget to the guest of the new key with a forced K_SET (`have` 3,
  `spacelogic::kSetAdopted`) every 2 s and keeps it as that guest's budget (`g_budget`, the store) until the guest's
  own K_MONEY reports it (`spacelogic::AdoptHold`, given up after 600 s); reports of another value meanwhile are
  not taken (`adopt_held`).
* The guest takes a K_SET with have 3 even when its budget is known (sets its money to it, notice "The host gave you
  your stored budget").
* Old-peer guard: have 3 only to guests whose HELLO lists space 1.3 or newer (`wire::space_rivalry::reads_adopt`,
  `spaceext::peer_families`); a 0.7.16.1 guest gets nothing new (`adopt_old_peer`), exactly as before.
* The host's stored budget carries the player's name (KeptStore entry name), which `coop.store adopt` looks up.
* Switch `coop.fix space.rival_adopt off`: 0.7.16.1. Wire: K_SET's have byte gets value 3 (wire/space_rivalry.h,
  space 1.3 lock rebased, no new kind). Status: `coop.space rivalry` adopted= adopt_sets= adopt_got= adopt_held=
  adopt_old_peer= adopt_pending=.

## Verification
open, waiting for the lab run.
`coop_space_rival_adopt.py` (suite `space_rival_adopt`, twin `space_rival_adopt_off`, fixture space): Rivals, the
guest earns 7000; it comes back as another game of the same name (`keptkey other on`: a new token, a newcomer with 0);
the host writes its store and `coop.store adopt Bob`: the guest's game money (cEmpire +0xD0) must be the stored budget
within 15 s and stay 10 s, the host's copy too. Expected: PASS with the fix; FAIL with SPORE_FIX_OFF=space.rival_adopt.
pending night run: space_rival_adopt space_rival_adopt_off (1 lane, about 14 min).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
