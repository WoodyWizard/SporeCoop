---
id: BUG-0262
title: space_rivalry: the guest's puppet is missing on the host's home planet (1 of 4 runs)
status: fixed
kind: test
stage: Space
reported: 2026-10-29
source: lab 20261029-051251 (main eea6b46)
role: all
versions: unreleased
fixed_in: 0.7.3.2
branch: fix/w9-space
commits: 1cc86fe
tests: space_rivalry (coop_space_rivalry.py)
signature:
docs: docs/06-testing.md#testing-space-world-identity
related:
---

## Report
Full lab run 20261029-051251 (lane 2, cold, main eea6b46 with wave 5 incl. BUG-0218's puppet identity change): "both on the home planet (no partner here)" and "the guest's puppet on the host belongs to no empire (me 800021CA)", so the PvP hit checks that follow failed too. Reruns 20261029-055430: 3/3 OK on lanes 2 and 4. Check first whether the host retired or never made the guest's puppet (fleet.puppet_id, BUG-0218) or the guest was not yet on the planet (test timing). Logs: out/lab/20261029-051251/space_rivalry*.

## Root cause
Observed (logs of 20261029-051251): the host's game had no puppet of the guest at all (`rivalry pol` printed only `me 800021CA`; "belongs to no empire" was a misleading message), because the two ships were never in the same place: after the fixture load both logged `hidden ... same_place=0` and no `appeared` followed, while the test spent ~3 min in `land_retry`. Inferred: one player's landing did not complete within one `land_retry` (150 s) right after the load (the GOG 2024 zoom-landing delay, docs/known-issues.md BUG-0181/0182). Not a mod bug; BUG-0218 not involved.

## Fix
Test only: two rounds of `land_retry` for whoever is still in the system, then wait until both ships are on the home planet and the host's game lists the guest's puppet; the failure messages name both places and "no puppet" separately from "no empire".

## Verification
no-fail-run: test timing (1 failure in about 10 lab runs; the slow landing cannot be forced). PASS out/lab/20261030-003001 (2 runs).

## History
- 2026-10-29: opened.
- 2026-10-30: test fixed on fix/w9-space (landing rounds + waits).
- 2026-11-01: released in 0.7.3.2.
