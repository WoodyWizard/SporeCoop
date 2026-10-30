---
id: BUG-0240
title: the host judges a guest's bite with only part of the game's damage checks (cell type and the hurt lock differ from the host's own bites)
status: fixed
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #174 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-cell
commits: 71eb1c7, 666f692
tests: cell_guest_bite
switch: pool.guestbite_type
signature:
docs: docs/re/cell.md#cell-damage-gate, docs/re/cell.md#cell-ko-grace, docs/known-issues.md
related: #174, BUG-0078
---

## Report
From GitHub issue #174: https://github.com/WoodyWizard/SporeCoop/issues/174 (the full analysis, links and the scenario are there).

BUG-0078 (#56, commits 0af9c83, 9413801, release 0.7.3.0) made the host apply a guest's bite (MSG_CELL_HIT) with DamageCell's gate. The gate copies only part of it. The cell-type part of IsDamageable (only "solid" cells are damageable) and its AI-type / avatar part are missing. The victim's `+0x17C` is read, but a guest bite applied by the mod never sets whatever the game sets after a bite. A guest can therefore damage cells the host's own bite cannot, and may be able to bite faster than the host. Severity: wrong behaviour, minor. The fix must not end up suppressing damage the guest should deal.

## Root cause
Observed (lane 2, `coop_cell_guest_bite.py` with `SPORE_FIX_OFF=pool.guestbite_type`): a rock (resource cell type 1) and a meat cell (cell type 2, AI type 0), each given hp 60: the host's own bite (DamageCell) took nothing, a guest's bite took 6 hp in the host's game. Plants and rocks have hp 6 in play, so a guest could bite them; a host cannot.

IsDamageable (GOG 2024 0xE52940, disassembled): ESI = victim; resource = GetData(+0x108); the AI block by difficulty (G+0x5190 +0x7C: 0 -> +0x248, 2 -> +0x194 when not -1, else +0xE0); true only when (ai.type != 0 or the victim is the avatar G+0x411C) and hp +0x244 != 0 and +0x112 != 1 and resource +0xB4 == 2. BUG-0078's gate copied the hp / dead part but not the resource part.

## Fix
The host's gate for MSG_CELL_HIT also spares a victim whose resource cell type is not 2 or whose AI type is 0 (the victim is never the avatar there). The resource is read once by a new `res_types(c)` that `kind_of` and `ai_type` now use too (holder released as before); unreadable data fails open. Switch `pool.guestbite_type` (separate from `pool.guestbite`). Test command `coop.pool kill <key|-> anyhit [kind]` (DamageCell on a cell of any kind, the nearest of a kind with `-`).

Not changed: the hurt lock. A host bite is followed by a lock the guest's bite does not set (the test waits 2 s between bites). Which byte the game sets (Bite sets +0x178 on both cells; `+0x17C` is checked) and for how long is not measured; writing a lock from the mod is not safe (who clears it is open). Listed in docs/known-issues.md.

## Verification
Two-game `tools/tests/coop_cell_guest_bite.py` (suite `cell_guest_bite`), lane 2: FAIL with `SPORE_FIX_OFF=pool.guestbite_type` (rock 60 -> 54, meat 60 -> 54); PASS twice with the fix, the plain creature control still loses hp.
* Lab: FAIL out/lab/20261028-235236 (`SPORE_FIX_OFF=pool.guestbite_type`: rock 60 -> 57, meat 60 -> 54); PASS out/lab/20261028-231910 and out/lab/20261028-235414.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-cell (71eb1c7): IsDamageable's cell type / AI type copied into the guest-bite gate; lock rate left open (known issue).
- 2026-10-29: released in 0.7.3.1.
