---
id: BUG-0270
title: Guest fault in the game's UI code while loading a shared Space world after a fixture jump (dc04aee5:0x0095904B, jump to 0x65)
status: open
kind: crash
stage: space
reported: 2026-10-29
source: lab 20261029-093233 campaign
role: guest
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature: dc04aee5:0x0095904B
docs:
related:
---

## Report
Lab 20261029-093233 campaign#2 (lane 4, out/lane4/campaign/20261029-093248, finding 4): after the
campaign's jump to the space fixture the host shared its world; Bob (a guest) loaded Madas.spo and
60 ms later caught `0xC0000005 at 0x00000065 (data 0x65)`, eax=0x65 (a call through a bad pointer),
calls 0095904B 00959832 0092245B 00922514 0095AB23 00812E3E ..., "called from" UTFWin::Image::GetProxyID
+0x1B0B < ... < UTFWin::UILayout::SetParentWindow+0x9BE. The game handled it (first chance) and went
on ("space: world epoch 2"); one fault, no crash report. The other guests had none. Not seen before
(`tickets.py find 0095904B`: nothing).

## Root cause
Not analysed. The UI code around 0x95904B runs during the world load; whether a co-op window
(the F9 lobby or the space page, open on the guest at the jump) is involved is open (?).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
