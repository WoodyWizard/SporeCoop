---
id: BUG-0802
title: Cell: a guest's rename of an imported cell sends only its metadata and the host crashes at 0xE83EC8
status: fixed
kind: crash
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-0455 (commit b3d24b5, lineages30-dev)
role: host guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-cell
commits: fa9fa072
tests: cell_meta_only
signature: 3010a413:0xE83EC8 read 0x9C (CreateCellObject for a partner's cell after its editor return)
docs: docs/re/creations.md#creations-meta-only
related: BUG-0455
---

## Report
Found by the fork (two-PC Steam session, 2026-11-08), not by our players yet. A guest opens the Cell editor
on a cell it got from somewhere else (an imported / partner cell), only changes the name and saves. The host's
game crashes at 0xE83EC8 when it builds the guest's new cell (the same signature as BUG-0455).

## Root cause
Code (confirmed by the run): make_bundle drops the pictures of a bundle without a model but kept a metadata-only
bundle; share_own re-reads only an EMPTY bundle; apply() registers any bundle with records. Observed (lab
20261112-065557, `creations.meta_only` off): a guest's cell bundle with its metadata record only (`coop.creations
sendmeta cell`, what the fork's 0.7.12 guest sent after renaming a cell whose model is in another package) made the
host register it as the guest's cell under a new key (7C8C6430) with no model behind it: the cell the host's game
builds for the guest from then on. No fault in the lab's 10 s (the guest's puppet was not rebuilt from it in that
time); the fork saw the fault at 0xE83EC8 (CreateCellObject) when it was. The real editor rename of an imported cell
was not repeated here (the lab fixture's cell is the same key in both profiles).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages BUG-0455, commit b3d24b5); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* A helper `has_cell_model(bundle)`: true when a record of type bem with the bundle's own group and instance and
  non-empty data is present.
* apply(): for kind Cell, a bundle with records but no model is refused (`g_last_error = "received Cell creation
  without its model"`), so the receiver keeps the partner's previous appearance (also covers an older sender).
  And an empty alias bundle for the Cell key already applied (`b.recs.empty() && ap.db && b.key == ap.key`)
  returns early so it does not unmount the package that supplies that key.
* share_own(): the re-read condition also fires for `k == kCell && !b.recs.empty() && !has_cell_model(b)`.
  After make_bundle, if the model is still missing, read the bem records of `{instance, bem, group}` from the
  game's mounted resources (`records_from_game`), else from the kept old records (`g_old_recs`, BUG-0370), and
  append them; if there is still no model, `g_waits_on_disk++` and try next round (never send).
* refresh_options(): for Cell, a metadata entry whose bem is not in EditorSaves but exists in the game
  (`key_exists`) or in `g_old_recs` also makes an option.
* own_key_here(): for Cell, when our applied own key maps to the original key and only the cached model has it,
  use the applied key.
* Their test: a real guest editor rename on two PCs; before: one metadata record sent, host fault at 0xE83EC8;
  after: two consecutive renames with two records each, zero faults, the guest kept its DNA and world.

## How to test
Lab, 2 instances, Cell: the guest imports a cell (or takes one from the host's world), opens its Cell editor,
changes only the name, saves. FAIL before: `coop.creations` on the guest shows a sent Cell bundle with 1 record
(metadata only) and the host logs a fault at 0xE83EC8 (or a refused / modelless puppet). PASS after: the bundle
has the bem and metadata records (2+), the host shows the renamed cell, zero faults; with the switch off the old
behaviour returns.

## Fix
creations.cpp `creations.meta_only`: make_bundle fills a bundle that has metadata but no model with the model from
the game's resources (`records_from_game` of {instance, bem, group}: the package that has it), else from the records
kept of an applied creation (BUG-0370); without a model the bundle has no records (share_own waits and tries again;
nothing is sent). apply() refuses a bundle with records but without its own model (log "came without its model ...
(BUG-0802)", `coop.creations` meta_only refused=); the player keeps the look it had. From the fork's idea (NOT FINAL
there), re-implemented; not taken: the empty-alias early return and the refresh_options / own_key_here changes.
Switch `coop.fix creations.meta_only off` (0.7.12).

## Verification
`coop_cell_meta_only.py` (suite `cell_meta_only`, ~1 min): the guest sends its cell's bundle with the metadata only,
three times; the host must not fault, must still draw the guest's cell and keep the guest's creation key it had;
the guest's own shares never send a cell without a model.
* FAIL with `SPORE_FIX_OFF=creations.meta_only`: lab 20261112-065557 (the host took key 7C8C6430 without a model).
* PASS: lab 20261112-063453 (refused=3, the key kept, no fault), 20261112-071436.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit, not reproduced by us).
- 2026-11-12 (fix/w48-cell): fixed (creations.meta_only), FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
