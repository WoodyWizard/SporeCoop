---
id: BUG-9041
title: Founder archive rejects valid run-compressed paint headers
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Automatic founding from native landfall continuation
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_founding.py
switch: creations.archive_runs
signature: Invalid creature model header while archiving native Stabbella
docs: docs/re/creations.md
related: BUG-9040
---

m2-auto-found-a.log reaches Tribal with the separate native landfall species,
then the host logs Invalid creature model header before creating a village.
decode_founder uses model_effects, a parser designed for in-place paint
substitution. That parser intentionally refuses run-compressed paint bytes.
Archive validation needs a bounded read-only decoder, not rewriting offsets.

The saved1934-byte native model has FE07 across its zero coat/detail layers.
The existing bounded byte-run decoder now reads38 header bytes and verifies
the BEM magic before extracting paints. In-place substitution retains its
stricter parser. Native layers are2F363540/0/0. Negative run a rejects this
header; run b passes the header then identifies BUG-9042. Runs d and cold-a
load the archived species into native villagers on both peers, with zero faults.
