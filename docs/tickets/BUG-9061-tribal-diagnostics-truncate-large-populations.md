---
id: BUG-9061
title: Tribal diagnostic output truncates large populations and invalidates checks
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: Religious graduation validation on a native alliance campaign
role: both
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_diagnostics.py
switch:
signature: coop.tribe response ends mid-member at 8191 bytes
docs: docs/re/tribe.md
related: BUG-0305
---

tribal-validation-alliance4b.log falsely reports that the 12-member band did
not synchronize. Native member vectors show 12 on both games, whereas the
guest console report ends during member54 and omits member55 and the host
village. tribal-validation-truncation.log records both responses ending at
exactly8191 bytes. SporeKit h_out_printf uses an8192-byte buffer; coop.tribe
prints its complete status and all villagers through one call. BUG-0305
already addresses the same mechanism for coop.npc by emitting bounded chunks.
Apply that pattern to the Tribal diagnostic, without changing gameplay.

The earlier alliance4.log missing-member assertion remains inconclusive:
the native population also fell from9 to8 during that attempt. Do not
retroactively count it as a passing performance or attribute the actual
population change to truncation. Capture complete diagnostics on rerun.

Fixed by emitting the assembled ASCII report in6000-byte chunks through the
existing output API. tribal-validation-diagnostics-pass.log verifies every
header's declared population against complete member lines in the same frame:
host8245 bytes, guest10934 bytes, seven tribes and52 villagers on each, zero
faults. The old captured8191-byte responses are the negative evidence. The
alliance4c rerun verifies12 guest musicians, actual instrument inputs, native
relation3-to7 and the fourth shared victory, with no missing-member assertion.
