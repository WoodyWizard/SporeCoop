---
id: BUG-9018
title: Tribal co-op dialog ignores clicks after a world reload
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo partner rebuild UI acceptance
role: both
versions: unreleased after duo9
fixed_in: unreleased
branch: duo-species
commits: 47b5523
tests: tools/tests/duo_tribe_capture_rebuild.py
switch: core.ui_tribal_clicks
signature: F9 opens with input_lock zero but Village button receives no native click
docs: docs/re/tribe.md
related: BUG-0273
---

## Evidence

FAIL build/local/m2-helper-rebuild-guest.log: the host opened F9 but the
Village button never opened its page. Repeated injected clicks, cursor moves
and closing/reopening F9 did not help. Native input_lock was zero and the
UI message log did not show the button receiving a click. The guest's page
worked. Native-window focus was not independently measured.

After a full process restart, the host's native click worked with the
proposed Tribal fallback disabled. The successful Rebuild button test
(m2-helper-rebuild-guest-final.log) recorded locked_clicks=0 and
locked_native=1: the fallback did not execute. Therefore it is not proof of
that candidate fix. The speculative fallback was removed. Preserve this
as a regression and investigate native focus/message delivery if it recurs;
do not claim the underlying input issue fixed.

## Verification

The failure recurred on the guest in the consolidated capture/rebuild test
(m2-capture-final-host.log). The next build restored the switchable fallback,
scoped to the visible two-village Tribal dialog. It waits for native delivery
and uses the existing native-delivery suppression before forwarding a missed
button click outside the frame.

FAIL: m2-click-control-host.log with core.ui_tribal_clicks off.
PASS: m2-click-control-host.log after enabling it in the same session, from
`duo_tribe_capture_rebuild.py --host 4 --guest 5 --victim host --click-control`.
The disabled click stays in the lobby. Enabling the fix opens the Village page
and increments the recovered-click counter. The helper then rebuilds through
F9; both native checkpoints and host walking pass. This qualifies the click
recovery path, not a diagnosis of the native game's swallowed-message cause.

Later raid investigation found a native registration dialog capturing input
on DuoQAM2Raid4/5 (qa-stage-5.png). Dismissing it restored native camera
movement, and combat passed again with normal input active
(m2-combat-unblocked.log). This explains the raid fixture's ignored input;
the earlier click-control run did not record its underlying native dialog,
so the same cause there remains an inference. Gameplay UI tests now check
for registration rather than relying on input_lock, which stayed zero.
