---
id: BUG-9134
title: Tribe / Civ: the connected player gets no village of their own when they play a custom creature
status: fixed
kind: bug
stage: Tribe Civ
reported: 2026-11-11
source: owner report on 0.7.8-lineages51-dev ("transition to Tribe ... the connected player loaded ... but no tribe"; "BigTexas's chief is not their creature")
role: guest
versions: 0.7.8-lineages46-dev to 0.7.8-lineages51-dev (since the 0.7.8 merge)
fixed_in: 0.7.8-lineages52-dev
branch: feat/lineages-47
commits:
tests: QA DuoQAClythHost (Clyth.spo, Tribe, its founder roster moved aside) + DuoQACreatureGuest with `coop.creations choose creature 291D7B07` (Duckulon, its own creation); host log "tribe automatic founders: ..."; coop.fix tribe.founder_parts off / on
signature: host log "tribe automatic founders: Invalid or mismatched guest creature data."
docs: -
related: BUG-0559
switch: tribe.founder_parts
---

## Cause
The automatic founders (Tribe; Civ uses the same archive) save the guest's creature with the founder roster
(creations::archive_guest_model -> decode_founder). decode_founder accepted at most 2 records (model and metadata).
Since the SporeCoop 0.7.8 merge (Lineages 46) pack() adds one description per part the model uses (BUG-0559, group
0x7C0DE5C1): a custom creature's bundle has 2 + N records (Duckulon: 13). Every custom creature was refused, the guest's
village was never made, and the guest went on in the host's tribe with the host's chief. Premade creatures (no records)
skipped the check, so the QA guest (a premade) never showed it.

## Fix
decode_founder accepts the part descriptions: counted in the archive's size check, not registered (the receiver's own
parts answer them), the model and metadata checked as before. The refusal now logs which check failed and the records.

## Verification (QA)
* off: "founder archive of player 1 refused: records 13, unpacked 1, key 291D7B07/2B978C46/40626200 crt 1, archive key
  7C0B35F6, expected 7C0B35F6" every 5 s, no guest village.
* on: "tribe automatic founders: Both species have their own villages. Saving and refreshing the shared campaign.", the
  guest's log "tribe: guest village created", the archive clyth.spo.duoguest-7C0B35F6 (12986 bytes) and the roster
  written; the save loaded again with no founder error.
* The QA refresh after the founding stopped on Spore's registration box (QA profile, closed with "don't prompt again"),
  and a hostile village raided the new guest village (captured after 30 s): gameplay of that save, not this bug.
* QA data: Clyth.spo in DuoQAClythHost was saved by these runs; its earlier roster is clyth.spo.duofounders.qa-bak.
