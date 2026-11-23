---
id: BUG-1340
title: 0.7.16 final check: the DNA lab tests dna_away, team, dna_resend, dna_notice compare the guest's DNA with the host's (the shared pool) while own DNA is the default
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w60-dna
commits: 1165c4fd
tests: dna_away, dna_away_shared, team, dna_resend, dna_resend_shared, dna_notice, dna_notice_shared
signature:
docs:
related: BUG-1291, BUG-1310, BUG-0151, BUG-0128, BUG-0389
---

## Report
The final check of rel/0.7.16 (rel/0.7.16 worktree out/lab/20261120-041304) failed four DNA tests on one line each:
* dna_away "the guest has the same total" (host 0.0 -> 18.0, guest 3.5 -> 21.5),
* team "the guest has the same DNA (8.5)" (host 0.0 -> 3.5),
* dna_resend "the guest has the host DNA total (14.9, host 0.0)",
* dna_notice "the guest's log names both causes" (guest ledger empty; the cause half is BUG-1341).
All of them ran warm (warm run 1-3 of their games) and passed on rel/main before (history 0 of 7, 0 of 3, 0 of 6).

## Root cause
A test bug: the tests were written for the shared pool (0.7.15) and compare the guest's DNA with the host's. Since
WP-D (BUG-1291) own DNA per player is the default (all.shared_dna = 0, Scope::Session): the host sends every guest its
own wallet. Observed in the run: every gain reached both games (dna_away host +18, guest +18: 3.5 -> 21.5; team host
+3.5, guest 5.0 -> 8.5), only the start differed: the guest's wallet survives the lane's warm reset (dna.cpp book_load:
a store opened without a record keeps the wallets in memory; the warm reset loads a fresh slot, "store: world
'game12/scalinus.spo' opened: 0 players, 0 records (no file)") while the host's DNA goes back to the save's 0, so the
guest's game starts with its earlier wallet ("dna: own 0 -> 4, bar 0.0 -> party 0.0 (kept) cause=unknown" after each
warm reset in dna_away.guest.game.log). That is the designed own-DNA behaviour, not a desync: the gates (BUG-1310)
compare the guest with the host's record of its wallet and found nothing. dna_notice's ledger regex read only the
shared line `dna: a -> b cause=` and not the guest's own line `dna: own a -> b, bar x -> party y cause=`.

## Fix
Tests only, no check dropped (tools/tests/dna_mode.py party_gain, one method for every DNA test): with own DNA the
guest's own DNA must grow by the same amount as the host's (the game's numbers on both machines, waited up to 4 s:
the total goes out per guest); with the shared pool (--shared: all.shared_dna on, dna_mode.setup) the guest must have
the host's total as before. dna_resend in own mode: the guest's settled total must come back after `coop.dna local 7`
(the host resends the wallet; with `coop.pool fix resend 0` it does not). dna_notice reads both log forms. New suite
variants dna_away_shared, dna_resend_shared, dna_notice_shared keep the shared-pool behaviour checked (one variant per
behaviour: own DNA default, shared on).

## Verification
Was waiting for the lab run (no lab today, owner rule). To run:
  SPORE_DETECTOR=20 python3 tools/lab.py run dna_away dna_away_shared team dna_resend dna_resend_shared dna_notice dna_notice_shared --lane 2 --lane 3 --retry 0 --no-build
Twins that must FAIL: SPORE_CELL_FIX=0 dna_resend (own) and with --shared (`SPORE_CELL_FIX=0 python3 tools/tests/coop_dna_resend.py [--shared]`), `coop_dna_away.py --nofix` (the 7 are lost).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): dna_away, dna_away_shared, team, dna_resend, dna_resend_shared, dna_notice and dna_notice_shared PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643 (the FAILs of the old checks: the final check named in the Report). The hand twins were not run again (test change only; the mod's switches unchanged). Closed.

## History
- 2026-11-20: opened.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
