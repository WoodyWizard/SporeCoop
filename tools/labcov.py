#!/usr/bin/env python3
"""What the lab has run (docs/06-testing.md#testing-coverage): `lab.py coverage` and `lab.py fixcheck`.

  coverage                  every suite test with its last recorded run (out/lab of the main checkout and
                            of every worktree), its result and how often it ran; tests that never ran are
                            flagged, and tests not run for --days
  coverage --commit <rev> --require smoke,full,multi
                            the release rule: which tests of the required suites ran on that commit and
                            passed; exit 1 when one is missing or failed
  fixcheck                  every "SPORE_FIX_OFF=<switch> fails" claim of suite.py against the recorded
                            runs with that switch off (the test output's `SPORE_FIX_OFF: <switch> off`
                            line): confirmed by a FAIL run, contradicted when it only ever passed,
                            unverified without a run; exit 1 on a contradiction
"""
import collections
import datetime
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(HERE, 'tests')]
import loggates  # noqa: E402

RUN_ID = re.compile(r'^(\d{8})-(\d{6})')


def run_time(rundir):
    m = RUN_ID.match(os.path.basename(rundir))
    return datetime.datetime.strptime(m.group(1) + m.group(2), '%Y%m%d%H%M%S') if m else None


def base_name(name):
    return name.split('#')[0]


def runs_by_test(dirs):
    """{test: [(time, rundir, data, result)]} oldest first"""
    out = collections.defaultdict(list)
    for rundir, data, r in loggates.recorded(dirs):
        t = run_time(rundir)
        if t is not None:
            out[base_name(r['name'])].append((t, rundir, data, r))
    for v in out.values():
        v.sort(key=lambda x: x[0])
    return out


def verdict(r):
    if r.get('skipped'):
        return 'skip'
    if r.get('ok'):
        return 'ok (flaky)' if r.get('flaky') else 'ok'
    return 'FAIL'


def coverage(tests, runs, now=None, days=7.0):
    """[(test, suites, last run or None, verdict, runs, passes, flag)]"""
    now = now or datetime.datetime.now()
    rows = []
    for t in tests:
        rs = runs.get(t['name'], [])
        passes = sum(1 for x in rs if x[3].get('ok') and not x[3].get('skipped'))
        if not rs:
            rows.append((t['name'], sorted(t['suites']), None, '', 0, 0, 'NEVER RUN'))
            continue
        tm, rundir, data, r = rs[-1]
        age = (now - tm).total_seconds() / 86400
        flag = f'not run for {age:.0f} days' if age > days else ''
        rows.append((t['name'], sorted(t['suites']), (tm, rundir, data), verdict(r), len(rs), passes, flag))
    return rows


def commit_rule(tests, runs, commit, require):
    """the tests of the required suites on a commit: [(test, verdict or 'missing')]"""
    want = [t for t in tests if set(require) & set(t['suites'])]
    out = []
    for t in want:
        rs = [x for x in runs.get(t['name'], []) if (x[2].get('head') or '').split(' ')[0].startswith(commit[:7])]
        out.append((t['name'], verdict(rs[-1][3]) if rs else 'missing'))
    return out


# ---------------------------------------------------------------- fix switch claims
CLAIM = re.compile(r'SPORE_FIX_OFF=([\w.]+(?:\s*/\s*[\w.]+)*)\s+(?:fails?|FAIL)\b')
ADD = re.compile(r"^add\('([\w]+)'")


def claims(suite_path):
    """[(test, switch, line)] of the "SPORE_FIX_OFF=x fails" claims in suite.py (a claim in a comment
    above an add() belongs to that test)"""
    out, pending = [], []
    for n, line in enumerate(open(suite_path), 1):
        found = [(s.strip(), n) for m in CLAIM.finditer(line) for s in m.group(1).split('/')]
        m = ADD.match(line)
        if m:
            for s, ln in pending + found:
                out.append((m.group(1), s, ln))
            pending = []
        elif line.lstrip().startswith('#'):
            pending += found
        else:
            pending = []
    return out


def fix_off_runs(dirs, switch):
    """[(rundir, test, ok)] of recorded results whose test output switched `switch` off"""
    needle = f'SPORE_FIX_OFF: {switch} off'
    out = []
    for rundir, data, r in loggates.recorded(dirs):
        _, tlog = loggates.result_logs(rundir, r)
        try:
            text = open(tlog, errors='replace').read()
        except OSError:
            continue
        if needle in text:
            out.append((rundir, base_name(r['name']), bool(r.get('ok')) and not r.get('skipped')))
    return out


def fixcheck(suite_path, dirs):
    """[(test, switch, line, fails, passes, verdict, evidence)] (runs of the claimed test only)"""
    rows = []
    cache = {}
    for test, sw, ln in claims(suite_path):
        if sw not in cache:
            cache[sw] = fix_off_runs(dirs, sw)
        rs = [x for x in cache[sw] if x[1] == test]  # runs of the claimed test itself
        fails = [x for x in rs if not x[2]]
        passes = [x for x in rs if x[2]]
        v = 'confirmed' if fails else 'CONTRADICTED' if passes else 'unverified'
        ev = fails or passes
        rows.append((test, sw, ln, len(fails), len(passes), v,
                     ', '.join(sorted({os.path.basename(x[0]) for x in ev}))[:200]))
    return rows


def default_dirs():
    import subprocess
    common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    return loggates.lab_dirs(os.path.dirname(common) if common else ROOT)


def cmd_coverage(a):
    import suite
    runs = runs_by_test(default_dirs())
    if a.commit:
        req = [s for s in a.require.split(',') if s]
        rows = commit_rule(suite.T, runs, a.commit, req)
        bad = [(n, v) for n, v in rows if not v.startswith('ok') and v != 'skip']
        print(f'# release rule: suites {", ".join(req)} on {a.commit}\n\n{len(rows)} tests, '
              f'{len(rows) - len(bad)} passed on this commit\n')
        for n, v in rows:
            if v != 'ok':
                print(f'- {n}: {v}')
        return 1 if bad else 0
    rows = coverage(suite.T, runs, days=a.days)
    never = [r for r in rows if r[6] == 'NEVER RUN']
    stale = [r for r in rows if r[6].startswith('not run')]
    print(f'# Lab coverage\n\n{len(rows)} suite tests; {len(never)} never ran, {len(stale)} not run for {a.days:g} '
          f'days\n\n| test | suites | last run | result | runs | passed | flag |\n|---|---|---|---|---|---|---|')
    order = sorted(rows, key=lambda r: (r[6] != 'NEVER RUN', not r[6], r[0]))
    for name, suites, last, v, n, ok, flag in order:
        lr = f'{last[0]:%Y-%m-%d %H:%M} {last[2].get("branch", "")} {(last[2].get("head") or "")[:7]}' if last else '-'
        print(f'| {name} | {",".join(suites)} | {lr} | {v} | {n} | {ok} | {flag} |')
    return 0


def cmd_fixcheck(a):
    rows = fixcheck(os.path.join(HERE, 'tests', 'suite.py'), default_dirs())
    print('| test | switch | suite.py line | FAIL runs | PASS runs | verdict | runs |\n|---|---|---|---|---|---|---|')
    for r in sorted(rows, key=lambda r: (r[5] != 'CONTRADICTED', r[5] != 'unverified', r[0])):
        print('| ' + ' | '.join(str(x) for x in r) + ' |')
    bad = [r for r in rows if r[5] == 'CONTRADICTED']
    print(f'\n{len(rows)} claims: {sum(r[5] == "confirmed" for r in rows)} confirmed, {len(bad)} contradicted, '
          f'{sum(r[5] == "unverified" for r in rows)} unverified')
    return 1 if bad else 0
