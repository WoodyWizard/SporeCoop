---
id: BUG-9311
title: Civ (host): a partner's own card unlocks (civ.partner_unlocks, 7Q A) also unlock the host's and the other seats' nations
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, DuoQACivHost Abber + DuoQACivGuest)
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9306, BUG-9310
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
Not analysed. Inferred: RefreshUnlocks 0xBF2510 (or what it reads: "the shared player history traits", BUG-9056) keeps
the card-derived result somewhere shared (not only the nation's +0x484), so the last nation computed with swapped
cPlayer cards (+0x10FC..+0x1104) decides every nation's card unlocks; or campaign_tick's order (partners first, the
home nation last) recomputes the home nation from a cache filled during the partner's call.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
