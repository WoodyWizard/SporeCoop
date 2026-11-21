#!/usr/bin/env python3
"""Lab verdicts, failure reasons, the flaky ledger and the repeat rule (docs/06-testing.md#testing-lab-verdicts).

Pure functions over test output and recorded results (no game, no lane): tools/lab.py calls them for every result
and for `lab.py flaky`; tools/tests/labverdict_selftest.py tests them (make test).

  verdicts   PASS, FAIL, INCONCLUSIVE (A15, owner decision 11): a test whose precondition did not happen prints
             `RESULT: INCONCLUSIVE <why>` (the older spelling `RESULT: SKIP <why>` means the same). It is never a
             pass: results.json keeps ok=True / skipped=True for it (the run is not failed by one), verdict
             INCONCLUSIVE; INCONCLUSIVE_STREAK of them in a row (the ledger, every worktree) fail the test
             (`FAIL lab: INCONCLUSIVE n runs in a row`) and list it as a ticket candidate. Neither half of a FAIL/PASS
             twin (labcov fixcheck leaves it out).
  reasons    the FAIL lines of a test; a test that printed RESULT: FAIL without one gets its campaign findings
             (`FINDING [stuck] ...`) or the bracket lines of the campaign's report as the reason (A6, TST-1: 46 of 144
             campaign failures had an empty reason), else a line that says no reason was printed
  twins      fix-off runs (SPORE_FIX_OFF, SPORE_LAB_FIXOFF) and runs with a planted old behaviour (TWIN_ENV) are
             recorded in results.json (`fix_off`, `twin`) and left out of every failure rate (A7)
  ledger     one flake history over every results.json of the main checkout and all worktrees (A7, TST-2)
  repeat     the stopping rule of `lab.py run X --repeat N` (A8, TST-8)
"""
import collections
import datetime
import math
import os
import re

PASS, FAIL, INCONCLUSIVE = 'PASS', 'FAIL', 'INCONCLUSIVE'
INCONCLUSIVE_STREAK = 3  # INCONCLUSIVE this many runs in a row (the last ones, any branch, twins and quick runs left out)

_RESULT_INC = re.compile(r'^\s*RESULT: (SKIP|INCONCLUSIVE)\b ?(.*)$', re.M)


def output_verdict(text):
    """(INCONCLUSIVE, why) when the test said its precondition did not happen (RESULT: INCONCLUSIVE / RESULT: SKIP)
    and printed no RESULT: FAIL, else (None, '')"""
    if 'RESULT: FAIL' in text:
        return None, ''
    m = _RESULT_INC.search(text)
    if not m:
        return None, ''
    why = m.group(2).strip().strip('()').strip() or 'the test gave no reason'
    return INCONCLUSIVE, why


# BUG-1379: environment trouble (the machine, not the mod: a game or the test display did not start; the lab runs such a
# test again, up to twice, and keeps the last result). Only the harness's own lines count: an AssertionError of a start
# wait (twoinst / coop_ui / the tests' restarts: "instance 3 did not start", "the guest did not start"), spore.sh's and
# headless.sh's lines, bwrap's. Before, any "did not start" in the output counted: a fix switch's description in the
# SPORE_FIX_OFF line of the twin tribe_chief_still_retry_off ("... the first take ... did not start") made both of its
# FAILs environment trouble and its third run's PASS the verdict (lab 20261120-130608).
_ENV_TROUBLE = re.compile(r"^(?:AssertionError: .*did not start\b.*"
                          r"|.*\bthe headless test display did not start\b.*"
                          r"|.*\bheadless shell did not start\b.*"
                          r"|\s*restart: the guest did not start\b.*"
                          r"|.*bwrap: Can't find.*)$", re.M)


def env_trouble(text):
    """True when the output says the game (or the display) did not start: a run the lab tries again"""
    return bool(_ENV_TROUBLE.search(text))


def verdict_of(r):
    """the verdict of a recorded result (results.json before the verdict field: skipped = INCONCLUSIVE)"""
    v = r.get('verdict')
    if v in (PASS, FAIL, INCONCLUSIVE):
        return v
    if not r.get('ok'):
        return FAIL
    return INCONCLUSIVE if r.get('skipped') else PASS


def was_inconclusive(r):
    """the test itself said INCONCLUSIVE (also when the streak rule made the result a FAIL)"""
    return bool(r.get('inconclusive')) or verdict_of(r) == INCONCLUSIVE


# ---------------------------------------------------------------- failure reasons
_MAKE_ERR = re.compile(r'^make(\[\d+\])?: \*\*\* \[[^\]]+\] Error \d+')
_FINDING = re.compile(r'^\s*FINDING \[(\w+)\] (.*)$')
# the campaign's report lists every finding as `    [kind] HH:MM:SS stage/phase: text` (coop_campaign.py)
_BRACKET = re.compile(r'^\s*\[(\w+)\] (\d\d:\d\d:\d\d) (\S+): (.*)$')


def reasons(text, failed=None):
    """the reason lines of a test's output: its FAIL lines; for a failed test without any, its non-info campaign
    findings as `FAIL <kind>: <text>` (or the report's bracket lines as `FAIL <kind> <stage>: <text>`), else a line
    saying that it printed no reason; a failed test without a RESULT line: the exception of its traceback, the lab's
    timeout. failed: the test failed (default: it printed RESULT: FAIL)"""
    lines = text.splitlines()
    out = [l.strip() for l in lines if l.strip().startswith('FAIL')]
    # `make test` (the lab's unit test): the part that failed first; its self-tests print planted FAIL lines that
    # would fill the reasons otherwise (lab 20261115-014727: unit-lint hidden behind lane 10's planted crashes)
    made = [f'FAIL {l.strip()}' for l in lines if _MAKE_ERR.match(l)]
    if made:
        return made + out
    if out:
        return out
    if failed is None:
        failed = 'RESULT: FAIL' in text
    if not failed:
        return []
    seen = set()
    for l in lines:
        m = _FINDING.match(l)
        if m and m.group(1) != 'info' and m.group(2) not in seen:
            seen.add(m.group(2))
            out.append(f'FAIL {m.group(1)}: {m.group(2).strip()}')
    if not out:
        for l in lines:
            m = _BRACKET.match(l)
            if m and m.group(1) != 'info':
                out.append(f'FAIL {m.group(1)} {m.group(3)}: {m.group(4).strip()}')
    if not out and 'Traceback (most recent call last)' in text:  # harness trouble: the exception is the reason
        tail = text[text.rindex('Traceback (most recent call last)'):].splitlines()
        exc = [l.strip() for l in tail[1:] if re.match(r'^[A-Za-z_][\w.]*(Error|Exception|Interrupt|Exit)\b', l)]
        out.append(f'FAIL harness: {exc[-1][:200] if exc else "a traceback"}')
    if not out and 'LAB: TIMEOUT' in text:
        out.append('FAIL lab: the test ran past its timeout')
    if not out and 'RESULT: FAIL' in text:
        out.append('FAIL (the test printed RESULT: FAIL without a FAIL line: see its log)')
    elif not out and 'RESULT:' not in text:
        out.append('FAIL harness: no RESULT line')
    return out


_SEGMENT = re.compile(r'^CAMPAIGN-SEGMENT (\{.*\})\s*$', re.M)


def segment_of(text):
    """A6: the failed segment a campaign printed (`CAMPAIGN-SEGMENT {json}`: stage, to, save, file, ...), or None"""
    import json
    m = _SEGMENT.search(text)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except ValueError:
        return None


# ---------------------------------------------------------------- twins (fix-off / planted old behaviour)
# environment knobs that switch a fix off or plant the old behaviour for a twin run (the names the tests use:
# SPORE_FIX_OFF, SPORE_CELL_FIX=0, CAMPAIGN_OLD_TRIBE=1, UI_GOLDEN_TWIN=..., CAMPAIGN_PLANT_NOCLICK=18, ...)
TWIN_ENV = re.compile(r'^(?:[A-Z0-9_]*_OLD(?:_[A-Z0-9_]+)?|OLD_[A-Z0-9_]+|[A-Z0-9_]*PLANT[A-Z0-9_]*|[A-Z0-9_]*_TWIN'
                      r'|SPORE_[A-Z0-9_]+_FIX|[A-Z0-9_]+_GUEST_CMDS)$')


def twin_env(env):
    """['NAME=value'] of the twin knobs set in an environment (a _FIX knob only when 0 / off)"""
    out = []
    for k in sorted(env):
        v = (env.get(k) or '').strip()
        if not v or k in ('SPORE_FIX_OFF', 'SPORE_LAB_FIXOFF') or not TWIN_ENV.match(k):
            continue
        if k.endswith('_FIX') and v.lower() not in ('0', 'off', 'no', 'false'):
            continue
        if not k.endswith('_FIX') and v.lower() in ('0', 'off', 'no', 'false'):
            continue
        out.append(f'{k}={v}'[:80])
    return out


_FIX_OFF_LINE = re.compile(r'SPORE_FIX_OFF[^:\n]*: *([\w.]+) off\b')


def fix_off_in_log(path, limit=1 << 16):
    """the switches a recorded test's output switched off (`SPORE_FIX_OFF: <x> off`, twoinst.start; results.json
    before the fix_off field)"""
    try:
        with open(path, 'rb') as f:
            text = f.read(limit).decode('utf-8', 'replace')
    except OSError:
        return ''
    return ','.join(sorted(set(_FIX_OFF_LINE.findall(text))))


def is_twin(r, log_fix_off=''):
    return bool(r.get('fix_off') or r.get('twin') or log_fix_off)


# ---------------------------------------------------------------- the ledger
RUN_ID = re.compile(r'^(\d{8})-(\d{6})')


def run_time(rundir):
    m = RUN_ID.match(os.path.basename(rundir.rstrip('/')))
    return datetime.datetime.strptime(m.group(1) + m.group(2), '%Y%m%d%H%M%S') if m else None


def is_release_branch(branch):
    return branch == 'main' or (branch or '').startswith('rel/')


Row = collections.namedtuple('Row', 'test time run branch commit verdict inconclusive reasons twin quick')


def rows_of(recorded, fix_off_of=None):
    """[Row] of recorded results ((rundir, data, result) as loggates.recorded gives them), oldest first.
    fix_off_of(rundir, r): the switches an older result switched off (its log), '' when none"""
    out = []
    for rundir, data, r in recorded:
        t = run_time(rundir)
        if t is None:
            continue
        twin = ''
        if r.get('fix_off'):
            twin = f'fix_off {r["fix_off"]}'
        elif r.get('twin'):
            twin = ' '.join(r['twin'])
        elif 'fix_off' not in r and fix_off_of is not None:
            fo = fix_off_of(rundir, r)
            twin = f'fix_off {fo}' if fo else ''
        out.append(Row(r['name'].split('#')[0], t, os.path.basename(rundir), data.get('branch') or '',
                       (data.get('head') or '').split(' ')[0], verdict_of(r), was_inconclusive(r),
                       list(r.get('first') or r.get('fails') or [])[:3], twin, bool(r.get('quick'))))
    out.sort(key=lambda x: (x.time, x.run))
    return out


Stats = collections.namedtuple('Stats', 'test runs fails inconclusive rel_runs rel_fails mixed_commits commits '
                                        'streak first last last_fail reasons twins quick')


def ledger(rows, since=None, branch=None):
    """{test: Stats} over rows: twins (fix-off, planted old behaviour) and quick runs are counted apart, never in
    runs / fails. since: a datetime; branch: a glob ('rel/*') the branch must match"""
    import fnmatch
    by = collections.defaultdict(list)
    twins, quick = collections.Counter(), collections.Counter()
    for x in rows:
        if since and x.time < since:
            continue
        if branch and not fnmatch.fnmatch(x.branch, branch):
            continue
        if x.twin:
            twins[x.test] += 1
            continue
        if x.quick:
            quick[x.test] += 1
            continue
        by[x.test].append(x)
    out = {}
    for test, xs in by.items():
        fails = [x for x in xs if x.verdict == FAIL and not x.inconclusive]
        inc = [x for x in xs if x.inconclusive]
        rel = [x for x in xs if is_release_branch(x.branch)]
        per_commit = collections.defaultdict(set)
        for x in xs:
            if x.commit and not x.inconclusive:
                per_commit[(x.branch, x.commit)].add(x.verdict)
        mixed = sum(1 for v in per_commit.values() if {PASS, FAIL} <= v)
        streak = 0
        for x in reversed(xs):
            if not x.inconclusive:
                break
            streak += 1
        last_fail = fails[-1] if fails else None
        out[test] = Stats(test, len(xs), len(fails), len(inc), len(rel),
                          sum(1 for x in rel if x.verdict == FAIL and not x.inconclusive), mixed,
                          len(per_commit), streak, xs[0].time, xs[-1].time,
                          last_fail, [f'{x.run} {x.branch}: ' + '; '.join(x.reasons)[:240] for x in fails[-3:]],
                          twins[test], quick[test])
    for test in set(twins) | set(quick):
        if test not in out:
            out[test] = Stats(test, 0, 0, 0, 0, 0, 0, 0, 0, None, None, None, [], twins[test], quick[test])
    return out


def inconclusive_streak(rows, test):
    """how many of the test's last results in a row were INCONCLUSIVE (twins and quick runs left out)"""
    n = 0
    for x in reversed(rows):
        if x.test != test or x.twin or x.quick:
            continue
        if not x.inconclusive:
            break
        n += 1
    return n


def ticket_candidates(stats, k=INCONCLUSIVE_STREAK):
    """tests INCONCLUSIVE in their last k runs or more: the precondition never happens (A15: counts as a failure)"""
    return sorted((s for s in stats.values() if s.streak >= k), key=lambda s: (-s.streak, s.test))


def known_line(s):
    """one line of a test's history for a failure in a run: 'known flake: 5 of 11 failed on rel/main since ...'"""
    if s is None or not s.runs:
        return 'no history (first run)'
    rel = (f'{s.rel_fails} of {s.rel_runs} failed on rel/main' if s.rel_runs else 'never run on rel/main')
    tail = f', {s.fails} of {s.runs} on all branches since {s.first:%Y-%m-%d}'
    mixed = f', mixed on {s.mixed_commits} commit(s)' if s.mixed_commits else ''
    inc = f', INCONCLUSIVE {s.inconclusive}' + (f' ({s.streak} in a row)' if s.streak else '') if s.inconclusive else ''
    kind = 'known flake' if s.fails and s.fails < s.runs - s.inconclusive else 'fails every run' if s.fails else 'history'
    return f'{kind}: {rel}{tail}{mixed}{inc}'


def rate(k, n):
    return f'{k}/{n}' + (f' ({100.0 * k / n:.0f} %)' if n else '')


def table(stats, top=40, only_failing=True):
    """the ledger as markdown rows, worst first (fail rate, then fails)"""
    xs = [s for s in stats.values() if s.runs and (s.fails or s.inconclusive or not only_failing)]
    xs.sort(key=lambda s: (-s.rel_fails, -s.fails, -s.streak, s.test))  # the release branches' failures first
    lines = ['| test | fails / runs | rel/main | mixed commits | INCONCLUSIVE (in a row) | twins | last failure |',
             '|---|---|---|---|---|---|---|']
    for s in xs[:top]:
        lines.append(f'| {s.test} | {rate(s.fails, s.runs - s.inconclusive)} | {rate(s.rel_fails, s.rel_runs)} | '
                     f'{s.mixed_commits} | {s.inconclusive} ({s.streak}) | {s.twins} | '
                     f'{(s.reasons[-1] if s.reasons else "")[:160]} |')
    return lines


# ---------------------------------------------------------------- the repeat rule (A8)
Z90 = 1.6449  # two-sided 90 %


def wilson(k, n, z=Z90):
    """(low, high) of the Wilson score interval of a rate k/n (0..1); (0, 1) for n = 0"""
    if n <= 0:
        return 0.0, 1.0
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return max(0.0, c - h), min(1.0, c + h)


UNTIL = ('clear', 'fail', 'all')
CLEAR_FAILS = 3  # FAIL in each of the first 3 decided runs: fails at least about half the time (90 % lower bound 0.53)


def repeat_stop(verdicts, planned, until='clear'):
    """(stop, why) after the finished copies of one repeated test (their verdicts, any order):
      until=fail   stop at the first FAIL (fail fast: a reproduction is all that is wanted)
      until=clear  stop once the answer can no longer change: a PASS and a FAIL both seen (the test is mixed: flaky
                   or a one-in-N bug), or FAIL in each of the first CLEAR_FAILS decided runs (it fails most of the
                   time), or INCONCLUSIVE in each of the first INCONCLUSIVE_STREAK runs (the precondition does not
                   happen); a test that only passes runs all N (a one-in-N bug needs them)
      until=all    never (the old behaviour; to measure a rate)"""
    p = sum(v == PASS for v in verdicts)
    f = sum(v == FAIL for v in verdicts)
    i = sum(v == INCONCLUSIVE for v in verdicts)
    if len(verdicts) >= planned:
        return False, ''
    if until == 'fail':
        return (True, 'fail fast: the first FAIL') if f else (False, '')
    if until != 'clear':
        return False, ''
    if p and f:
        return True, 'clear: both PASS and FAIL seen (mixed)'
    if f >= CLEAR_FAILS and not p:
        return True, f'clear: FAIL in each of {f} runs'
    if i >= INCONCLUSIVE_STREAK and not p and not f:
        return True, f'clear: INCONCLUSIVE in each of {i} runs (the precondition does not happen)'
    return False, ''


def repeat_line(test, verdicts, planned, why=''):
    """the rate of a repeated test: 'repeat x: FAIL 2 of 7 (29 %, 90 % interval 9-59 %), ...'"""
    p = sum(v == PASS for v in verdicts)
    f = sum(v == FAIL for v in verdicts)
    i = sum(v == INCONCLUSIVE for v in verdicts)
    n = p + f
    lo, hi = wilson(f, n)
    s = (f'repeat {test}: FAIL {f} of {n}' + (f' ({100.0 * f / n:.0f} %, 90 % interval {100 * lo:.0f}-{100 * hi:.0f} %)'
                                             if n else '') + (f', INCONCLUSIVE {i}' if i else '')
         + f'; {len(verdicts)} of {planned} runs')
    return s + (f', stopped early ({why})' if why else '')
