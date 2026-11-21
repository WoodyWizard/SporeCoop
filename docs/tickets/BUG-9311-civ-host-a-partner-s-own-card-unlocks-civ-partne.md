---
id: BUG-9311
title: Civ (host): a partner's own card unlocks (civ.partner_unlocks, 7Q A) also unlock the host's and the other seats' nations
status: fixed
kind: bug
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, DuoQACivHost Abber + DuoQACivGuest)
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-civ
commits: ffe972b0
tests: manual two-game live test (docs/06-testing.md#testing-partner-superweapon); no lab test yet
signature:
docs: docs/re/civ.md#civ-partner-sw-tools
related: BUG-9306, BUG-9310, BUG-9312
---

## Report
Live QA 2026-11-21 (QA agent, TITAN QA copy, build fix/l47-qa = fix/l47-powers aeaedfc3 + BUG-9308), host DuoQACivHost /
Abber (Civ; the host's own cards all 0, every nation unlocked=000000000000), guest DuoQACivGuest = seat 3 (see BUG-9309
for the seat). Host `coop.cards set 1 2 60A78928` (the guest's tribe card): log "civ: seat 3's nation unlocks from its own
cards cell 00000000 creature 00000000 tribe 60A78928 (7Q A)", and within 1 s `coop.civ sw` showed unlocked=000010000000
(type 4) for seat 3 **and for seat 0 (the host's nation, "cards=the host's own") and seats 1 / 2** (shared, no record).
`coop.cards clear 1`: every nation kept type 4; `coop.cards set 1 2 E5F355B3`: every nation 000000000000 again. Sampled
6 times over 4 s after a set: seat 0 and seat 3 always equal. `coop.fix civ.partner_unlocks off`: unchanged (type 4 on
both). Expected (7Q A): seat 3 unlocked differs from seat 0 in the card-based types.

## Root cause
First reading (not analysed; superseded by the CORRECTION below). Inferred: RefreshUnlocks 0xBF2510 (or what it reads: "the shared player history traits", BUG-9056) keeps
the card-derived result somewhere shared (not only the nation's +0x484), so the last nation computed with swapped
cPlayer cards (+0x10FC..+0x1104) decides every nation's card unlocks; or campaign_tick's order (partners first, the
home nation last) recomputes the home nation from a cache filled during the partner's call.

CORRECTION (2026-11-21, fix/l47-civ): no cache in the game. RefreshUnlocks 0xBF2510 (asm from +5, the dump has the
mod's jmp) reads the cPlayer's cards +0x10FC / +0x1100 / +0x1104 directly on every call (0xBF254D..0xBF2575, each
through 0x555E80 into a trait list) and keeps nothing. The leak is the mod's: h_Unlocks (civ.cpp, 7Q A) wrote the
partner's cards into the host's own cPlayer around the call and closed the swap ledger with `game::swap_close(mark, 3)`,
which only DROPS the entries the caller restores itself (swap_ledger.h close()); nothing restored them. So after the
first partner run the host's cPlayer kept that partner's cards: the home nation (computed last, without a swap), the
other seats (no record: the "shared" cards read from the same cPlayer), the guests (wsync.stages copies the host's
cards) and the host's own tribe powers / save all saw them. That explains each observation of the report: `coop.cards
clear 1` kept type 4 everywhere (the cPlayer still held 60A78928), `set 1 2 E5F355B3` cleared it everywhere (the next
leak), `civ.partner_unlocks off` changed nothing (no swap any more, the leaked card stayed). Live (switch off, below):
the host's `coop.cards` shows its own record empty but `effective ... tribe=60A78928` (read from its cPlayer).

## Fix
fix/l47-civ ffe972b0, `civ.unlock_cards_restore` (on): h_Unlocks keeps the three values it replaces and writes them
back into the cPlayer after RefreshUnlocks (before closing the ledger entries). `coop.fix civ.unlock_cards_restore off`:
the old leak (tests). civ.partner_unlocks was right in itself; the switch it names still gates the swap.
Saves written while the leak was live (the host's world shares and autosaves of a 7Q A build with a partner record)
may hold a partner's card in the host's cPlayer; nothing repairs such a save (not seen in the QA save: the host's
effective cards were 0 at load).

## Verification
Live, TITAN QA copy (build fix/l47-civ ffe972b0), host DuoQACivHost / Abber (instance 9) + guest DuoQACivGuest
(instance 10) = seat 3, the setup of BUG-9310. Host `coop.cards set 1 2 60A78928` (the guest's own tribe card), then
`coop.civ sw` at 1.5 / 3 / 4.5 s on the host and once on the guest, `coop.cards` on both.
- FAIL (switch off on both games from the start): every nation unlocked=000010000000 (type 4) on the host (seat 0 "the
  host's own" included, seats 1 / 2 "shared (no record)") and on the guest; the host's `coop.cards`: own record empty,
  `effective ... tribe=60A78928` (its cPlayer had taken the partner's card).
- PASS (switch on): host seat 3 unlocked=000010000000 (its own tribe card), seats 0, 1, 2 000000000000, the same at all
  three samples; the host's effective cards stayed 00000000; on the guest its own nation (seat 3) 000010000000 and
  seats 0 / 1 / 2 000000000000. Seat 3 differs from seat 0 (decision 7Q A).

## History
- 2026-11-21: opened.
- 2026-11-21: root cause found (the host's cPlayer cards were never put back after a partner's unlock run); fixed on fix/l47-civ (civ.unlock_cards_restore); live FAIL / PASS on TITAN.
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
