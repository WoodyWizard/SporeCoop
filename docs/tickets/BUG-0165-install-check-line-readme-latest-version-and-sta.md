---
id: BUG-0165
title: INSTALL check line, README latest version and stability-report lane count are out of date
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#144
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: ee9b778
tests: docs (README, INSTALL, stability-report): checked by reading
signature:
docs:
related: #144
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/144 (full text, discussion).

Three instructions or statements no longer match the code. A player or contributor who follows them literally is misled:
- INSTALL.md tells players to look for `loaded coop v0.6.0` in the log to confirm the install.
- README says the latest version is 0.7.2.
- The stability report tells contributors to run the nightly suite on 8 lanes, which the lab silently caps to 6.

### Where
- INSTALL "Check that it works": https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/INSTALL.md#L186-L190
- The real log line: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/mods.cpp#L66 with the version https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L3-L4
- README "Status and roadmap": https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/README.md#L208
- Stability report command: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/stability-report.md#L125
- The lab's cap: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L137-L142

### How to reproduce
**How to reproduce / check**

- Install 0.7.2.4 and read `mods\logs\sporekit.log`.
- Run `grep -n "latest" README.md`.
- Run `python3 tools/lab.py run nightly --lanes 8` (prints the cap line).

### Expected
- INSTALL names the line without a fixed version (`loaded coop v<version>`).
- README names the current release, or no patch number.
- The stability report uses `--lanes 6` (or mentions `SPORE_MAX_LANES` and the GPU risk).

## Root cause
**What happens**

1. INSTALL.md: "`SporebinEP1\mods\logs\sporekit.log` exists and has a line `loaded coop v0.6.0`." SporeKit writes `loaded %s v%s (gen %d)` with the mod's version, now `loaded coop v0.7.2.4 (gen 1)`. A player searching the log for the documented line does not find it and may think the install failed.
2. README: "Version 0.7 (latest 0.7.2): co-op in every stage ...". The current release is 0.7.2.4 (`COOP_VERSION`, tag v0.7.2.4, CHANGELOG).
3. stability-report.md: `tools/lab.py run nightly --lanes 8  # everything, ~35 min on 8 lanes`. lab.py caps the lanes at 6 since the GPU-reset incident (docs/06-testing.md 6.19): `lab: 8 lanes capped to 6 ...`. The run then takes longer than the stated ~35 min. Getting 8 lanes needs `SPORE_MAX_LANES`, which the doc does not mention.

### Confidence
Confirmed by reading. A second reviewer verified it.

## Fix
- INSTALL.md: "a line `loaded coop v<version> (gen 1)` with the version you installed".
- README: no patch number in "Status and roadmap" (links the Releases page), so it cannot go stale.
- stability-report.md: `--lanes 6` with the cap and SPORE_MAX_LANES named.

## Verification
By reading: `grep -n "loaded coop" docs/INSTALL.md`, `grep -n latest README.md`, the
stability-report command. No automated check: the texts no longer name a version that can go out
of date (a docs_lint version check would have nothing left to compare).

### Related
#47 (netcode doc vs code), #75 (release job does not check the tag against `COOP_VERSION`).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #144.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
