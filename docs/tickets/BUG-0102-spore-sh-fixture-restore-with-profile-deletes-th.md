---
id: BUG-0102
title: spore.sh fixture restore with profile "-" deletes the player's own saves
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#80
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 14463ee
tests: tools/tests/tools_selftest.py fixture (make test)
signature:
docs:
related: #80
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/80 (full text, discussion).

`spore.sh fixture` accepts `-` as profile, meaning the player's own Spore profile (`AppData/Roaming/Spore`), not a test profile. With `restore` that runs `rm -rf` on the player's own `Games` folder (all their saves) and replaces it with the fixture. On lane 1 (the real install, the default lane) this is the owner's real game data. CLAUDE.md says tests and tools must not touch the player's own profile.

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/spore.sh#L211-L231

### How to reproduce
By reading; do not run it on a real install. Under `SPORE_LANE=1`, `tools/spore.sh fixture restore creature -` resolves `prefix` to `<bottle Games>/drive_c/users/<user>/AppData/Roaming/Spore` and removes `Games` there.

### Expected
`restore` and `slot` refuse the player's own profile (only `save` may read from it), or at least move the old `Games` folder to a dated backup first.

## Root cause
**What happens**

```bash
cmd_fixture() {
    local op="${1:-}" name="${2:-creature}" profile="${3:-test}"
    local prefix="$APPDATA_DIR/SporeKit-$profile/Spore"
    [[ "$profile" == "-" ]] && prefix="$APPDATA_DIR/Spore"
    ...
        restore) rm -rf "$prefix/Games" && tar -C "$prefix" -xzf "$FIXTURES/$name.tgz" && echo "restored fixture $name" ;;
        slot) ... rm -rf "$prefix/Games/Game$slot" ...
```

`-` is offered in the usage line (`fixture save|restore [name] [profile|-]`) and is useful for `save` (capture a fixture from the owner's own game). For `restore` and `slot` it deletes the owner's saves, or one of their save slots, without a backup or a question. If the `.tgz` is missing, `rm -rf` has already run when `tar` fails. Nothing in the repo calls it with `-` today; the risk is a person or an agent following the usage line.

### Confidence
CONFIRMED by reading.

## Fix
`spore.sh fixture restore|slot` exits 1 for the profile `-` (only `save` may read from the
player's own profile), refuses profile names other than `[A-Za-z0-9_-]+`, and checks that the
fixture file exists before anything is deleted. Usage line: `fixture save [name] [profile|-] |
restore [name] [profile] | slot name profile N`.

## Verification
`tools/tests/tools_selftest.py fixture`: HOME pointed at a temporary folder with a fake "Games" bottle holding
`Spore/Games/Game0/x`, SPORE_FIXTURES at a temporary fixture.
- FAIL with main's spore.sh: restore into "-" exits 0 and deletes `Game0/x`; slot too.
- PASS with the fix: refused, the save stays; restore into a test profile still works.

### Related
CLAUDE.md section 3 ("Tests must not touch the player's own Spore profile (only the test profiles)") and section 2 ("Never change files of the save that is loaded").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #80.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
