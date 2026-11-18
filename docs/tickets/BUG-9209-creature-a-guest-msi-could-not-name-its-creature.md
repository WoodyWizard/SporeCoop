---
id: BUG-9209
title: Creature: a guest (MSI) could not name its creature in the pre-game start editor, so it played its old creature
status: fixed
kind: bug
stage: Creature
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 07868e39 cc7b1269
tests: tools/qa_win (manual, 2026-11-15): TITAN two instances and the MSI (pre-game editor naming, unsaved exits)
signature:
docs:
related: BUG-9207 BUG-9208
---

## Report
Owner play test 2026-11-15 (3 players, a new Creature world from the galaxy wizard): "MSI was unable to name their creature, therefor they werent able to join with their own creature."

## Root cause
Observed (the MSI log): the pre-game start editor opened at 02:09:31 with the host's parts list; the check unlocked at 02:13:29; the guest left the editor at 02:19:11 without a save, the world loaded, the old pick 'Lilku' played, the BUG-9164 editor followed in the world and 'Yankin' was saved there at 02:22:28 (named there). The MSI opened Settings (the Graphic page) twice while in the editor (02:17). Titan (this PC) named 'Slank' in the same editor. Not known yet why the name could not be typed; the Settings keyboard capture (coop_ui graphics_input) only runs while Settings shows. Second fault: a guest leaving the pre-game editor without a save plays its old pick (creations::needs_first_creature is false while g_first_pending is still set), against BUG-9208's rule.

## Fix
- The guests' pre-game editor waits until the galaxy menu's intro bars (letterbox_top / letterbox_bottom) are gone: opened under them, the bars stayed and took the clicks meant for the name field (switch creature.editor_after_intro).
- Leaving that editor without saving brings the base creature's editor back once (not the old creature); a second unsaved exit loads the world as before, where the player plays and can edit (switch creature.first_retry). The single retry keeps a player whose name field does not open from being trapped (see below).
- Still open: on the MSI's long-used test profile (MSIGuest) the name field of the pre-game editor never opens (the panel rises, the edit window C7CEB1BD stays hidden, so typing goes nowhere and the check says a name is needed). A fresh profile on the same PC, TITAN at 1280 x 960, TITAN with ui_scale=100, the Creepy & Cute pack, a background window and the profile's old SporeCoopportrait packages were each ruled out (all name fine or still fail as stated). Preferences.prop matches a working profile. Listed in docs/known-issues.md#issue-pregame-name-field.

## Verification
FAIL (before 07868e39, the owner's MSI, 2026-11-15 night): the pre-game editor under the intro bars, name not typeable.
PASS (TITAN, joined guest with no creature in the host's world, 1024 x 768 and 1280 x 960, fresh profile too): the name field opens with the dice and takes the typed name.
FAIL (MSI, profile MSIGuest, cc7b1269 not yet): unsaved exit reopened the same editor every time.
PASS (MSI, cc7b1269): the editor came again once; the second unsaved exit loaded the world and the player played its creature.
no-fail-run: Windows QA by hand (no lab on this PC); the MSIGuest name field itself is not fixed (known issue).

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
- 2026-11-15: owner: "The box said 'Name Your Creature' and when I clicked it, it raised the name menu (showing description and tags) however it still said 'Name Your Creature' at the top. Upon clicking that, it closed that name menu. It was like I was unable to even access the naming field." (the MSI, 1280 x 960). The click on the panel's name field toggled the panel instead of focusing the field: to reproduce on the MSI.
- 2026-11-15 (overnight tour): reproduced on the MSI (profile MSIGuest) in a joined pre-game editor; not on TITAN nor with a fresh MSI profile. Retry limited to once (cc7b1269) so nobody is trapped; residual kept as a known issue.
