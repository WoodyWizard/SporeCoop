---
id: BUG-0124
title: a fixture that lacks its facts never fails the test (check_fixture result ignored)
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#103
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-lab
commits: 824813c, 6f2d12e, f8b9733, bb7154e
tests: tools/tests/lab_selftest.py (make test)
signature:
docs: docs/10-dev-pipeline.md 10.3/10.4, docs/06-testing.md
related: #103
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/103 (full text, discussion).

`docs/10-dev-pipeline.md` lists "a fixture lacks what the test needs" as an invariant that fails every test. In `twoinst.py`, `check_fixture` only prints the missing facts, and both callers drop its return value. A test on a broken or stale fixture runs on, and can pass or fail for the wrong reason.

### Where
- The claim: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/10-dev-pipeline.md#L66-L74
- `check_fixture`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/twoinst.py#L185-L189
- Return value ignored (warm path): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/twoinst.py#L257
- Return value ignored (cold path): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/twoinst.py#L332
- `Checks.faults()`, which never looks at fixtures: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/twoinst.py#L345-L354
- The facts: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/fixtures.py#L52-L62

### How to reproduce
**How to reproduce / check**

1. Change one fact regex in `tools/tests/fixtures.py` so it cannot match (for example the `tribe` fixture's tribe count).
2. Run `SPORE_LANE=N python3 tools/tests/coop_tribe.py`. The log shows `(fixture tribe lacks: ...)` and the test still ends with `RESULT: OK`.

No game is needed to see it in the code: no caller uses the return value.

### Expected
A missing fixture fact makes the test FAIL, or ends it with a distinct harness error that the lab reports as such, as the doc says.

## Root cause
**What happens**

- The claim (10.4 "Invariants (checked in every test)"): "`twoinst.start()` starts a monitor for both games; `Checks.faults()` fails the test when: ... a fixture lacks what the test needs (`tools/tests/fixtures.py` facts)."
- The reality:

  ```python
  def check_fixture(host, fixture):
      bad = [what for what, ok in fixtures.check(host, fixture) if not ok]
      if bad:
          print(f'  (fixture {fixture} lacks: {", ".join(bad)} - tools/tests/fixtures.py)', flush=True)
      return not bad
  ```

  `start()` calls `check_fixture(host, fixture)` as a statement on both paths and returns the consoles. `Checks` has no reference to the result. `Checks.faults()` reads only `exc` and `Monitor.events`.

### Confidence
Confirmed by reading. A second reviewer verified it.

## Fix
`check_fixture` stores what is missing in `twoinst.FIXTURE_BAD`; `Checks.faults()` prints `FAIL invariant: fixture <name> lacks ...`. A warm reset whose fixture lacks its facts falls back to a cold start.

Suggested in the review:

- Have `start()` remember the result (for example `twoinst.FIXTURE_BAD = bad`) and make `Checks.faults()` print `FAIL invariant: fixture <name> lacks ...` and set `ok = False`.
- Or raise right in `start()`, so a bad fixture is a clear harness failure.
- Also fix the warm path: a warm reset onto a fixture missing its facts should fall back to a cold start or fail.

## Verification
A self-test of the tool (no game): case fixture (a fake console that answers nothing, fixture tribe). `python3 tools/tests/lab_selftest.py` (in `make test`): all cases PASS on fix/w4-lab; `--tree <main checkout>` (main 7874654): this case FAILs (every case except clean / harness_once and the main-checkout game_log lines fails there). Smoke on lane 2 with the new lab: out/lab/20261028-030019 (13/14, the one FAIL was the self-test's own fake fault line seen by the outer lab, fixed in f8b9733).

Planned in the review:

The step above (one fact broken) must give `RESULT: FAIL` with the fixture line. Restored, it must pass.

### Related
#14, #20, #46 (invariants the lab and twoinst claim but do not enforce).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #103.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-lab (wave 4), test: lab_selftest.py.
- 2026-10-28: released in 0.7.3.0.
