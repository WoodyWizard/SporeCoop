---
id: BUG-9067
title: Absent founder cities automatically transfer command to the host
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-06
source: Approved separate-command policy and Vantiegan disconnect control
role: host guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_civ_offline_control.py
signature: guest capital 21E9 immediately assigned commander0 after disconnect
docs: docs/re/civ.md
related: BUG-9054
---

The host automatically receives both capital commands when the guest leaves
the new religious Civilization (civ-offline-before-state.log). The approved
default reserves the absent nation's units and cities. A host-enabled option
may temporarily command them until the guest returns. Native factions, city
ownership vectors and saved species must survive every toggle and reconnect.

The F9 Nations page saves the host's choice per planet and founding factions.
When enabled, GetSelected, BoxSelect and ApplySelection borrow both countries'
selectable vectors only for the native call, with rollback on a native fault.
The native political ownership vectors stay authoritative.

PASS offline9067-input-b.log: native city and vehicle ownership refuse the
host by default. The actual F9 control enables box selection and a ground move
of the absent partner's vehicle. Rejoining restores command with both species
and factions intact. offline9067-cold.log verifies the saved option in fresh
processes. The first UI driver tried to click before the Nations button was
visible at 10 FPS and now waits for the button. Both pairs exited cleanly.
