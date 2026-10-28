---
id: BUG-0097
title: release job does not check tag against COOP_VERSION, and notes for vX.Y.Z take every X.Y.Z.N section
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#75
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: b28f978
tests: tools/tests/tools_selftest.py release (make test)
signature:
docs:
related: #75
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/75 (full text, discussion).

The `release` job publishes whatever the build produced under the tag's name and never checks that the tag matches `COOP_VERSION` in `mods/coop/coop.cpp`. The in-game updater takes the version from the release tag, so a tag pushed before the bump (a manual step in CLAUDE.md section 4) makes every player see "SporeCoop X is available" on every start, while pressing Update fails with "the release pack is not newer". Also, the release notes for a tag like `v0.7.2` are cut from CHANGELOG.md with a prefix match and contain every `0.7.2.N` section above it.

### Where
- The job: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.github/workflows/build.yml#L42-L63
- Notes extraction: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.github/workflows/build.yml#L58-L60
- Version from the tag: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/pack.cpp#L236-L242
- Check and apply: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L58 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L79-L82
- The zip and pack are named after `COOP_VERSION`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/package.sh#L104

### How to reproduce
Notes, on the current CHANGELOG.md:

```sh
for v in 0.7.3 0.7.2.1 0.7.2; do
  awk -v v="$v" '$0 ~ "^## \\[?"v"\\]?" {on=1; next} /^## / && on {exit} on' CHANGELOG.md | wc -l
done
```

Output: `0` (no section, silent), `25` (right), `168` (0.7.2 gets 0.7.2.4, .3, .2, .1 and 0.7.2 itself).

Version mismatch: reading the workflow and `autoupdate.cpp` as above.

### Expected
The release job fails before publishing when `${GITHUB_REF_NAME#v}` differs from `COOP_VERSION` or when CHANGELOG.md has no `## [<ver>]` section; the notes contain exactly that version's section.

## Root cause
**What happens**

1. Version mismatch. `package.sh` names `SporeCoop-<COOP_VERSION>.zip/.pack`, and the pack header carries `COOP_VERSION`. The job uploads `dist/SporeCoop-*.zip` etc. under `$GITHUB_REF_NAME`. With tag `v0.7.5` and `COOP_VERSION "0.7.4"` (bump forgotten), clients on 0.7.4 get `compare_versions("0.7.5", "0.7.4") > 0` -> "available", download the pack, and `compare_versions(p.version = "0.7.4", "0.7.4") <= 0` -> "the release pack is not newer". That repeats on every start, and the release page says 0.7.5 while the files say 0.7.4.
2. Notes. `awk -v v="$ver" '$0 ~ "^## \\[?"v"\\]?" {on=1; next} /^## / && on {exit} on'`: `\]?` is optional, so `## [0.7.2.4]` matches `v=0.7.2`; every following `## [0.7.2.N]` matches the first rule again (`on=1; next`) before the exit rule, so the extraction runs through all of them. The dots in `v` also match any character. If CHANGELOG.md has no section for the tag, `notes.md` is empty and the release is still published.

### Confidence
Notes: CONFIRMED by running. Version mismatch: CONFIRMED by reading.

## Fix
`tools/release_check.sh vX.Y.Z [notes.md]`: fails when the tag is not `COOP_VERSION`, cuts exactly
the `## [X.Y.Z]` section of CHANGELOG.md (literal heading, `index(...) == 1`, so `0.7.2` no longer
takes the `0.7.2.N` sections) and fails when it is missing or empty. The build job runs it first on a
tag (fails before any artifact is built), the release job before publishing.
docs/10-dev-pipeline.md#pipeline-release-check. For v0.7.3.0: bump `COOP_VERSION` to "0.7.3.0" and add
`## [0.7.3.0]` to CHANGELOG.md, else the job stops.

## Verification
`tools/tests/tools_selftest.py release`: a temporary tree with COOP_VERSION and a CHANGELOG that has 0.7.3.0, 0.7.3,
0.7.2.4 and 0.7.2 sections.
- FAIL with main's workflow step (as a script): a mismatched tag passes, v0.7.2 takes 0.7.2.4 too, a
  missing section passes.
- PASS with the fix: v0.7.3.0 gives exactly its own section; mismatch and missing section fail.
Also run on the real tree: `tools/release_check.sh v0.7.2.4` gives its 42 lines.

### Related
CLAUDE.md section 4 (Releases: bump `COOP_VERSION`, push a tag `vX.Y.Z`: GitHub Actions builds and publishes).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #75.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
