---
id: BUG-9024
title: Purchased guest tool rack moves after native save reload
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo purchase persistence acceptance
role: host
versions: duo10-dev
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_tool_persistence.py
switch: tribe.tool_slots
signature: native rack ownership and purchase history persist but placement moves on load
docs: docs/re/tribe.md
related: BUG-9023
---

The F9 purchase succeeds and replicates, charging the guest exactly once. Cold
restart of DuoQAM2ToolPurchases4/5 retains native owner/faction and purchased bit,
but changes rack position from (122.867,433.251,-227.611) to
(117.804,436.894,-223.209) on both peers. `m2-tool-persistence.log` fails the
position assertion.

Native C99A50 relocates racks to nearest community slots during reload. The
purchase now selects and occupies an empty native slot before placing the rack.
NearestSlot AFACF0 takes layout and Vector3 pointer; SetObject AF9FB0 retains
the rack and marks the slot occupied. See docs/re/tribe.md for call-site evidence.

PASS `m2-tool-slot-purchase.log` and `m2-tool-slot-persistence.log` on
DuoQAM2ToolSlots4/5. The F9 purchase checks still pass, and cold host restart
plus guest transfer preserves owner, location and native purchase history.
The original `m2-tool-persistence.log` is the failing unpatched control.
