---
id: BUG-9309
title: Store: a salt.txt the host could not open for a moment was replaced by a new salt: every stored player of the host lost (NET-12)
status: open
kind: bug
stage: All
reported: 2026-11-22
source: audit
role: host
versions: 0.7.16.1
fixed_in:
branch: fix/w63-net
commits:
tests: store_salt_held, store_salt_held_off, store_tests (test_unreadable)
signature:
docs:
related:
---

## Report
NET-12 of the external static audit of 0.7.15.5 (verified row in out/research/ext-audit-verify-net-core.md: REAL,
very rare, high impact). Unchanged on rel/0.7.16 (store.cpp read_file 78-95, salt() 131-148 at 5376d388). A host
whose salt.txt another program held with write access at the first ask of a run (a sync or backup tool, an
antivirus) got a new salt written over the old one: every stored player of every world of that host had another key
from then on and their stored things (Space colonies, kept stores, Civ cities) were never found again.

## Root cause
Inferred from the code (Win32 sharing rules; Wine implements them the same): `read_file` opened with FILE_SHARE_READ
only, so an open by another handle with write access is a sharing violation; any open failure returned false, and
`salt()` took false for "no file", made a new salt and replaced salt.txt (tmp + MoveFileEx REPLACE_EXISTING, which a
holder with FILE_SHARE_DELETE allows). The same in `open_world`: an unreadable world file was an empty store, written
over the file at the next save; a ReadFile error was "damaged" (the file moved to .bad).

## Fix
Every share flag on the reads; `read_file` says Text / Missing / Unreadable (only ERROR_FILE_NOT_FOUND and
ERROR_PATH_NOT_FOUND are missing, a failed size or ReadFile is unreadable). The decisions are portable
(player_store.h `salt_step`, `world_step`, unit-tested): a new salt only when salt.txt is missing (written without
replacing a file that came meanwhile) or holds no salt (kept as salt.txt.bad); unreadable: no keys now, read again
after 2 s. A world file that cannot be read keeps its world's store closed (no write) and is read again every second.
`coop.store status` counts `unreadable=`. Switch `coop.fix store.unreadable_kept off` (unsafe: its off side loses
the stored players): as 0.7.16. Test command `coop.store saltprobe` (host): holds salt.txt as such a tool does while
the salt is asked for, says whether the guest's key and the file stayed, then puts the old salt back.

## Verification
Open, waiting for the lab run. Unit: store_tests test_unreadable (in `make test`, both sides of the decisions).
Lab: `tools/lab.py run store_salt_held store_salt_held_off`: store_salt_held must PASS, the twin store_salt_held_off
(SPORE_FIX_OFF=store.unreadable_kept) must FAIL at checks 2 and 3 (a new salt, salt.txt replaced).

## History
- 2026-11-22: opened.
