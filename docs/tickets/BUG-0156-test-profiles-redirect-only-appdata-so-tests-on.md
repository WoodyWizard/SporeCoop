---
id: BUG-0156
title: test profiles redirect only AppData, so tests on lane 1 can write into the owner's own Documents (My Spore Creations)
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#135
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 850644d
tests: tools/tests/kit_profile_docs.py (lab kit_docs; --old fails)
signature:
docs:
related: #135
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/135 (full text, discussion).

`-sporekit-profile:<name>` (and the implied `inst<N>` profile) redirects the game's user data by hooking `SHGetFolderPathW` for `CSIDL_APPDATA` only. Spore also writes outside AppData: the "My Spore Creations" folder in the user's Documents (PNG cards of saved creations, screenshots). Lane 1 is the real install in the owner's own "Games" bottle, so editor tests there can write into the owner's own Documents. CLAUDE.md: "Tests must not touch the player's own Spore profile (only the test profiles)".

### Where
- The profile hook, `CSIDL_APPDATA` only: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/instance.cpp#L22-L32
- Only `SHGetFolderPathW` is hooked (no A variant, no `SHGetSpecialFolderPathW`, no known-folder API): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/instance.cpp#L48-L61
- Lane 1 is the owner's real install and bottle: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/spore.sh#L6-L18
- The lab uses lane 1: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L143

### How to reproduce
1. Note the contents of Documents\My Spore Creations in the "Games" bottle.
2. Run an editor test on lane 1 (`tools/lab.py run coop_guest_editor --lanes 1`), which saves a creation in a test profile.
3. Check whether new PNGs appeared in the owner's Documents\My Spore Creations (and whether test profile folders `AppData\Roaming\SporeKit-*` hold a Documents equivalent: they do not).

### Expected
A test profile redirects every per-user folder the game writes to (AppData and Documents at least), so tests never add or change files in the owner's own folders.

## Root cause
**What happens**

- With a profile, `SHGetFolderPathW(CSIDL_APPDATA)` gets `\SporeKit-<name>` appended; every other folder id is passed through unchanged.
- Spore keeps "My Spore Creations" (creation PNGs, screenshots) under the Documents folder. Tests that open and save in editors (`coop_editor_*`, `coop_guest_editor`, `coop_vote`, the creations tests) or take game screenshots on lane 1 therefore write into the Documents folder of the owner's own bottle, next to the owner's own creations.
- Lane bottles N>1 are copies of the "Games" bottle; if Documents in that bottle is a link to the home folder (Wine's default desktop integration), all lanes write there.

### Confidence
PLAUSIBLE, severity low. CONFIRMED by reading that only `CSIDL_APPDATA` is redirected. That Spore writes into Documents through `SHGetFolderPathW(CSIDL_PERSONAL)` (and not another API) is not verified in this game build. A second reviewer verified the reading.

## Fix
`h_SHGetFolderPathW` (sporekit/instance.cpp) also redirects CSIDL_PERSONAL, CSIDL_MYPICTURES,
CSIDL_MYVIDEO and CSIDL_MYMUSIC while a profile is active: to
`<AppData>\SporeKit-<profile>\Documents[\Pictures|Videos|Music]` (built from the real AppData, the
folders are created). The game imports only `SHGetFolderPathW` of the shell folder APIs (checked in
the GOG 2024 exe's import table) and the hook is on the shell32 export, so every caller goes through
it. The log says `profile '<name>': Documents in ...` once. Switch: `-sporekit-profile-docs:0` (the
old behaviour, AppData only). `spore.sh run -x <arg>` passes extra game arguments.

## Verification
`tools/tests/kit_profile_docs.py` (lab `kit_docs`), lane 3: one game with a fresh test profile;
the game must make its "My Spore Creations" under the profile's Documents by itself at start (it
does, through CSIDL_PERSONAL: the log line shows the Documents redirect), and nothing in the
bottle's own Documents/My Spore Creations may change.
- FAIL `--old` (`-sporekit-profile-docs:0`): "the game made My Spore Creations in the profile" FAIL.
- PASS with the fix (lane 3, 2026-10-28).
Note: at start the game did not write into the existing owner folder either way (the folder
already existed); the files it writes later (creation cards, screenshots) go to the same CSIDL.

### Related
- #80 (spore.sh fixture restore with profile "-" deletes the player's own saves)
- #81 (more places that can kill the owner's own Spore on lane 1)

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `docs_old.log` (fix/w4-tools, --old), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261028-031641 on fix/w4-tools (kit_docs ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #135.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
