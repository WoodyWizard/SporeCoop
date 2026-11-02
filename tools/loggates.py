#!/usr/bin/env python3
"""Gates on the games' own logs (docs/06-testing.md#testing-log-gates): what the mod writes into
sporekit*.log while a test runs, judged after the test by tools/lab.py run_one.

  sync-check  `sync-check: MISMATCH` (a lasting desync the mod's own digest compare found; forced
              ones of `coop.diag sync force` are cut out by the test's LAB-MARK lines)
  hitch       `perf: hitch N ms (<activity>)` over 2 s while a stage is played: not during a load, a
              save, a received world, an editor, a cinematic, the galaxy menu, the first seconds
              after a stage change, nor the frame of the switch itself (logged up to 0.5 s before its
              `stage:` line, BUG-0448), nor the 2.0 s step of SporeKit's frame source switch (Present
              lost: frames come from the game loop, sporekit/d3d9hook.cpp kPresentLostSec)
  mem-low     `mem: the game's address space runs low`
  monitor     a `MONITOR-VIOLATION <invariant> <who>: <text>` line of the test's invariant monitor
              (tools/tests/monitor.py, docs/06-testing.md#testing-monitor); allow(..., kind=<invariant>)
  as-slope    the free address space of the per-minute `perf: fps avg ...` lines falls faster than
              N MB per minute (least squares over the test's minutes in one game process, minutes
              without a load / editor / received world only)

Every gate can be tuned or allowed per test in tools/tests/suite.py (`gates=`, `allow=`): an allow
names a ticket and a reason and is printed in the result, never silent.

  loggates.py report [<out/lab dir> ...]   report only: run the gates over recorded lab results
                                           (default: out/lab of the main checkout and of every
                                           worktree), print which tests would turn red and why
  loggates.py scan <game log> [...]        the findings of whole game logs (no window)
"""
import datetime
import glob
import json
import os
import re
import sys

GATES = ('sync-check', 'hitch', 'mem-low', 'as-slope', 'monitor')
DEFAULTS = {
    'hitch_ms': 2000,          # a hitch longer than this while a stage is played fails
    'hitch_settle_s': 20.0,    # seconds after a stage change / load / boot in which hitches are loads
    'hitch_switch_s': 0.5,     # a hitch logged this soon BEFORE a stage line is that switch's own frame (BUG-0448)
    'present_lost_ms': (2000, 2040),  # SporeKit's frame source switch: a 2.0 s step, not a stall
    'slope_mb_min': 25.0,      # address space lost per minute (least squares) that fails
    'slope_points': 4,         # at least this many per-minute samples in one process
    'slope_span_min': 3.0,     # spanning at least this many minutes
    'slope_settle_s': 90.0,    # samples this soon after a stage change / load / boot are the load's own growth
    'force_tail_s': 15.0,      # a forced mismatch shows up to this long after `sync force off`
}
STAGES = ('Cell', 'Creature', 'Tribe', 'Civilization', 'Space')
BUSY = ('loading', 'saving', 'receiving', 'editor', 'cinematic', 'nothing controlled', 'vote')

TOD = re.compile(r'^(\d\d):(\d\d):(\d\d)\.(\d{3}) ')
SESS = re.compile(r'^\S+ \[s (-?[\d.]+)\] ')
MISMATCH = re.compile(r"sync-check: MISMATCH with (?P<peer>the host|player \d+ '[^']*') \((?P<stage>.+?) "
                      r"(?P<world>[0-9A-F]{8}), (?P<info>[^)]*)\): (?P<text>.*)$")
HITCH = re.compile(r'coop: perf: hitch (\d+) ms \((.*)\)\s*$')
PERF = re.compile(r'coop: perf: fps avg (-?\d+) min (-?\d+) .*?longest frame (-?\d+) ms, hitches (-?\d+) '
                  r'\| address space (-?\d+) of (-?\d+) MB free, largest free block (-?\d+) MB \| (.*?)\s*$')
MEMLOW = re.compile(r"mem: the game's address space runs low: (-?\d+) MB free, largest free block (-?\d+) MB")
SETTLE = re.compile(r'coop: stage: |coop: world: loading|SporeKit [\d.]+ booting|world: the game saved box')
PRESENT_LOST = 'frames: Present has not reached SporeKit'
MARK = re.compile(r'^LAB-MARK (\S+) (\S+) ([\d.]+)', re.M)


# ---------------------------------------------------------------- reading a log
class Line:
    __slots__ = ('t', 'tod', 'text', 'n')

    def __init__(self, t, tod, text, n):
        self.t, self.tod, self.text, self.n = t, tod, text, n


def tod_of(raw):
    m = TOD.match(raw)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) + int(m.group(4)) / 1000 if m else None


def read_lines(path, offset=0):
    """the log's lines from a byte offset (a partial first line is dropped)"""
    with open(path, 'rb') as f:
        if offset:
            f.seek(max(0, offset - 1))
            if f.read(1) != b'\n':
                f.readline()
        data = f.read()
    return data.decode('utf-8', 'replace').splitlines()


def timed(raw_lines, end_time):
    """[Line] with absolute times (epoch seconds): the logs only have the time of day, so the last
    timed line is put on the day of end_time (the moment the log was last written, local clock) and
    the earlier ones are walked back over midnight"""
    tods = []
    day = 0
    prev = None
    for raw in raw_lines:
        tod = tod_of(raw)
        if tod is not None:
            if prev is not None and tod < prev - 43200:
                day += 1
            prev = tod
            tods.append(tod + 86400 * day)
        else:
            tods.append(None)
    last = next((x for x in reversed(tods) if x is not None), None)
    out = []
    if last is None:
        return [Line(None, None, raw, i) for i, raw in enumerate(raw_lines)]
    d = datetime.datetime.fromtimestamp(end_time)
    midnight = datetime.datetime(d.year, d.month, d.day).timestamp()
    # the game's clock (Wine's time zone) was an hour off the lab's around a DST change (2026-10-29:
    # the last line 23:01, the copy 22:01): the last line is put on the latest half hour step of its
    # time of day that is not after end_time (a running game wrote it in the last minutes)
    lim = end_time + 60
    last_abs = lim - ((lim - (midnight + last % 86400)) % 1800)
    base = last_abs - last
    cur = None
    for i, (raw, x) in enumerate(zip(raw_lines, tods)):
        if x is not None:
            cur = base + x
        out.append(Line(cur if x is not None else cur, x, raw, i))
    return out


# ---------------------------------------------------------------- what a mismatch is
def mismatch_kinds(text):
    """'t: 3 missing on the guest (t11 t12 t13), p: 1 apart (p1 22.3)' -> ['t missing', 'p apart']"""
    kinds = []
    for m in re.finditer(r'(?:^|, )([a-z?]): \d+ (missing on the guest|extra on the guest|apart) \(', text):
        kinds.append(f'{m.group(1)} {m.group(2).split()[0]}')
    for m in re.finditer(r'(?:^|, )([a-z_ ]+?): host [-\d.]+, guest [-\d.]+', text):
        kinds.append(f'{m.group(1)} total')
    if re.search(r'(?:^|, )vote: host ', text):
        kinds.append('vote')
    if 'vote editor: host' in text:
        kinds.append('vote editor')
    return kinds or ['other']


def is_host_side(text):
    return 'MISMATCH with player ' in text


# ---------------------------------------------------------------- the gates
class Finding(dict):
    """gate, who, t, line, stage, kinds, detail"""


def forced_windows(test_out, tail):
    """[(from, to)] epoch seconds in which the test forced a sync-check difference on purpose
    (`LAB-MARK sync-force on|off <time>` lines in its output, twoinst.lab_mark)"""
    wins, on = [], None
    for m in MARK.finditer(test_out or ''):
        if m.group(1) != 'sync-force':
            continue
        if m.group(2) == 'on' and on is None:
            on = float(m.group(3))
        elif m.group(2) == 'off' and on is not None:
            wins.append((on - 1.0, float(m.group(3)) + tail))
            on = None
    if on is not None:
        wins.append((on - 1.0, float('inf')))
    return wins


def scan(lines, who, cfg=None, forced=()):
    """the findings of one game's lines (already cut to the test's window)"""
    c = dict(DEFAULTS, **(cfg or {}))
    out = []
    settle_until = -1.0
    present_lost_at = None
    samples = []  # (t, free MB, activity, segment): one segment per stay in a stage of one process
    seg, seg_from = 0, None
    # the frame that switches the mode (an editor opening) is logged as a hitch of the stage it leaves,
    # just before the `stage:` line (vote_net_4p 20261101-223011: 2522 ms 29 ms before Creature -> Editor)
    switches = [ln.t for ln in lines if ln.t is not None and 'coop: stage: ' in ln.text]
    for ln in lines:
        s = ln.text
        if SETTLE.search(s) and ln.t is not None:
            settle_until = ln.t + c['hitch_settle_s']
            seg += 1
            seg_from = ln.t
        if PRESENT_LOST in s:
            present_lost_at = ln.t
        if 'MISMATCH' in s:
            m = MISMATCH.search(s)
            if m:
                if ln.t is not None and any(a <= ln.t <= b for a, b in forced):
                    continue
                out.append(Finding(gate='sync-check', who=who, t=ln.t, line=s.strip(), stage=m.group('stage'),
                                   kinds=mismatch_kinds(m.group('text')), host_side=is_host_side(s),
                                   detail=f'{m.group("stage")}: {", ".join(mismatch_kinds(m.group("text")))}'))
            continue
        m = HITCH.search(s)
        if m:
            ms, act = int(m.group(1)), m.group(2)
            if ms <= c['hitch_ms']:
                continue
            stage = act.split(',')[0].strip()
            if stage not in STAGES or any(b in act for b in BUSY):
                continue
            if ln.t is not None and ln.t <= settle_until:
                continue
            if ln.t is not None and any(0 <= t - ln.t <= c['hitch_switch_s'] for t in switches):
                continue
            lo, hi = c['present_lost_ms']
            if lo <= ms < hi:  # the frame source switch, not a stall
                continue
            if present_lost_at is not None and ln.t is not None and abs(ln.t - present_lost_at) < 0.1:
                continue
            out.append(Finding(gate='hitch', who=who, t=ln.t, line=s.strip(), stage=stage, kinds=['hitch'],
                               detail=f'{stage}: hitch {ms} ms'))
            continue
        m = MEMLOW.search(s)
        if m:
            out.append(Finding(gate='mem-low', who=who, t=ln.t, line=s.strip(), stage='', kinds=['mem-low'],
                               detail=f'address space low: {m.group(1)} MB free, block {m.group(2)} MB'))
            continue
        m = PERF.search(s)
        if m and ln.t is not None:
            free, total, act = int(m.group(5)), int(m.group(6)), m.group(8)
            stage = act.split(',')[0].strip()
            if free >= 0 and total > 0 and stage in STAGES and not any(b in act for b in BUSY if b != 'vote') \
                    and (seg_from is None or ln.t - seg_from >= c['slope_settle_s']):
                samples.append((ln.t, free, stage, seg))
    for p in sorted({x[3] for x in samples}):
        pts = [(t, f, st) for t, f, st, q in samples if q == p][1:]  # the first: the minute of the load
        sl = slope(pts, c)
        if sl is not None and -sl > c['slope_mb_min']:
            out.append(Finding(gate='as-slope', who=who, t=pts[-1][0], line='', stage=pts[-1][2], kinds=['as-slope'],
                               detail=f'address space {-sl:.0f} MB/min lost over {len(pts)} minutes '
                                      f'({pts[0][1]} -> {pts[-1][1]} MB free, {pts[-1][2]})'))
    return out


def slope(pts, c=DEFAULTS):
    """MB per minute (least squares) of [(t, free MB, stage)], None when too few / too short"""
    if len(pts) < c['slope_points'] or (pts[-1][0] - pts[0][0]) / 60 < c['slope_span_min']:
        return None
    xs = [(t - pts[0][0]) / 60 for t, _, _ in pts]
    ys = [f for _, f, _ in pts]
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    den = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else None


# ---------------------------------------------------------------- per test: allow-lists
def allowed(f, allows):
    """the allow entry of suite.py that covers a finding, else None"""
    for a in allows or ():
        if a['gate'] != f['gate']:
            continue
        if a.get('stage') and a['stage'] != f.get('stage'):
            continue
        if a.get('kind') and a['kind'] not in f.get('kinds', ()):
            continue
        if a.get('who') and a['who'] != f['who']:
            continue
        if a.get('match') and not re.search(a['match'], f.get('line', '') + ' ' + f.get('detail', '')):
            continue
        return a
    return None


def verdict(findings, allows):
    """(FAIL lines, allowed lines) of a test's findings: one line per gate, the first finding shown"""
    fails, notes = [], []
    by = {}
    for f in findings:
        a = allowed(f, allows)
        by.setdefault((f['gate'], a['ticket'] if a else None), []).append((f, a))
    for (gate, ticket), lst in sorted(by.items(), key=lambda x: (x[0][0], x[0][1] or '')):
        who = ', '.join(sorted({f['who'] for f, _ in lst}))
        first = lst[0][0]
        kinds = sorted({k for f, _ in lst for k in f.get('kinds', ())} - {gate})
        text = (f'gate {gate}: {len(lst)} on {who}' + (f' ({"; ".join(kinds)})' if kinds else '')
                + f': {first["detail"] if gate != "sync-check" else first["line"][first["line"].find("sync-check"):]}')
        if ticket:
            notes.append(f'allowed {text[:240]} [{ticket}: {lst[0][1]["reason"]}]')
        else:
            fails.append(f'FAIL lab: {text[:300]}')
    return fails, notes


# ---------------------------------------------------------------- the lab's view of one test
def log_marks(path):
    """what run_one needs to cut a game log later: its size and first line (a game that started
    again during the test wrote a new log: then all of it belongs to the test)"""
    try:
        with open(path, 'rb') as f:
            first = f.readline()
            f.seek(0, 2)
            return {'size': f.tell(), 'first': first.decode('utf-8', 'replace').strip()}
    except OSError:
        return None


def window_lines(path, mark=None, start=None, end=None):
    """the lines of a game log that belong to the test: from the offset of `mark` (log_marks at the
    test's start) when the same process still writes it, else the time window [start, end]"""
    end_time = end or os.path.getmtime(path)
    if mark:
        try:
            with open(path, 'rb') as f:
                first = f.readline().decode('utf-8', 'replace').strip()
        except OSError:
            first = None
        off = mark['size'] if first == mark['first'] else 0
        return timed(read_lines(path, off), end_time)
    lines = timed(read_lines(path), end_time)
    if start is None:
        return lines
    return [ln for ln in lines if ln.t is not None and start <= ln.t <= end_time + 1]


MONITOR_LINE = re.compile(r'^MONITOR-VIOLATION (\S+) (\S+): (.*)$', re.M)


def monitor_findings(test_out):
    """the invariant monitor's violations printed by the test (tools/tests/monitor.py)"""
    return [Finding(gate='monitor', who=m.group(2), t=None, line=m.group(0).strip(), stage='', kinds=[m.group(1)],
                    detail=f'{m.group(1)}: {m.group(3)}') for m in MONITOR_LINE.finditer(test_out or '')]


def check_logs(logs, cfg=None, allows=None, test_out='', marks=None, start=None, end=None):
    """{who: game log path} -> (fails, notes, findings)"""
    c = dict(DEFAULTS, **(cfg or {}))
    forced = forced_windows(test_out, c['force_tail_s'])
    findings = monitor_findings(test_out)
    for who, path in sorted(logs.items()):
        if not path or not os.path.exists(path):
            continue
        lines = window_lines(path, (marks or {}).get(who), start, end)
        findings += scan(lines, who, cfg, forced)
    findings = [f for f in findings if f['gate'] not in c.get('off', ())]
    fails, notes = verdict(findings, allows)
    return fails, notes, findings


# ---------------------------------------------------------------- report over recorded runs
def lab_dirs(root):
    """out/lab of the main checkout and of every worktree under SporeCoop-wt"""
    spore = os.path.dirname(root)
    dirs = [os.path.join(root, 'out', 'lab')]
    wt = os.path.join(spore, 'SporeCoop-wt')
    for dp, dn, _ in os.walk(wt):
        if dp.endswith(os.path.join('out', 'lab')):
            dirs.append(dp)
            dn[:] = []
        elif dp.count(os.sep) - wt.count(os.sep) >= 4 or os.path.basename(dp) in ('build', 'third_party', '.git', 'sdk', 'mods'):
            dn[:] = []
    return [d for d in dirs if os.path.isdir(d)]


def recorded(dirs):
    """(run dir, results.json data, result) of every recorded test result"""
    for d in dirs:
        for run in sorted(os.listdir(d)):
            p = os.path.join(d, run, 'results.json')
            try:
                data = json.load(open(p))
            except (OSError, ValueError):
                continue
            for r in data.get('results', []):
                yield os.path.join(d, run), data, r


def result_logs(rundir, r):
    """{who: path} of a result's copied game logs"""
    base = os.path.basename(r.get('log') or f'{r["name"]}.log')[:-len('.log')]
    out = {}
    for who in ('host', 'guest', 'extra1', 'extra2'):
        p = os.path.join(rundir, f'{base}.{who}.game.log')
        if os.path.exists(p):
            out[who] = p
    return out, os.path.join(rundir, base + '.log')


def judge_recorded(rundir, r, cfg=None, allows=None):
    """the gates over a recorded result: the window is the test's (its log's last write minus its
    seconds; results.json from this branch on stores the exact log offsets)"""
    logs, tlog = result_logs(rundir, r)
    if not logs:
        return None
    try:
        end = os.path.getmtime(tlog)
        out = open(tlog, errors='replace').read()
    except OSError:
        return None
    start = end - max(0, r.get('seconds', 0) - 3)
    return check_logs(logs, cfg, allows, out, r.get('log_marks'), start, end + 7)


def report(dirs, as_json=None):
    import collections
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tests'))
    try:
        import suite
        tests = {t['name']: t for t in suite.T}
    except ImportError:
        tests = {}
    rows = []
    for rundir, data, r in recorded(dirs):
        name = r['name'].split('#')[0]
        t = tests.get(name, {})
        got = judge_recorded(rundir, r, t.get('gates'), t.get('allow'))
        if got is None:
            continue
        fails, notes, findings = got
        rows.append(dict(run=os.path.relpath(rundir, os.path.dirname(os.path.dirname(dirs[0]))) if dirs else rundir,
                         name=name, ok=r.get('ok'), branch=data.get('branch', ''), fails=fails, notes=notes,
                         findings=[{k: v for k, v in f.items() if k != 'line'} | {'line': f['line'][:200]} for f in findings]))
    if as_json:
        json.dump(rows, open(as_json, 'w'), indent=1)
    total = len(rows)
    passed = [x for x in rows if x['ok']]
    red = [x for x in passed if x['fails']]
    print(f'# Log gates over recorded lab results (report only)\n\n{total} results with game logs, '
          f'{len(passed)} passed; {len(red)} of the passed would turn red\n')
    per_test = collections.defaultdict(lambda: dict(n=0, red=0, gates=collections.Counter(), kinds=collections.Counter(),
                                                    last=''))
    for x in passed:
        pt = per_test[x['name']]
        pt['n'] += 1
        if x['fails']:
            pt['red'] += 1
            pt['last'] = x['run']
        for g in {f['gate'] for f in x['findings']}:
            pt['gates'][g] += 1
        for k in {f'{f.get("stage", "")}: {k}' for f in x['findings'] if f['gate'] == 'sync-check' for k in f['kinds']}:
            pt['kinds'][k] += 1
    print('## Passed tests that would fail\n\n| test | red / passed runs | gates (runs) | sync-check kinds (runs) | last red run |')
    print('|---|---|---|---|---|')
    for name, pt in sorted(per_test.items(), key=lambda kv: (-kv[1]['red'], kv[0])):
        if not pt['red']:
            continue
        print(f'| {name} | {pt["red"]}/{pt["n"]} | ' + ', '.join(f'{g} {n}' for g, n in pt['gates'].most_common())
              + ' | ' + ', '.join(f'{k} {n}' for k, n in pt['kinds'].most_common(5)) + f' | {pt["last"]} |')
    # mismatch kinds per stage
    kinds = collections.defaultdict(lambda: [0, set(), set()])
    for x in passed:
        for f in x['findings']:
            if f['gate'] != 'sync-check':
                continue
            for k in f['kinds']:
                e = kinds[(f.get('stage', ''), k)]
                e[0] += 1
                e[1].add((x['run'], x['name']))
                e[2].add(x['name'])
    print('\n## sync-check MISMATCH kinds per stage (passed results)\n\n| stage | kind | lines | results | tests |')
    print('|---|---|---|---|---|')
    for (st, k), (n, res, names) in sorted(kinds.items(), key=lambda kv: -len(kv[1][1])):
        print(f'| {st} | {k} | {n} | {len(res)} | {", ".join(sorted(names)[:8])}{" +" + str(len(names) - 8) if len(names) > 8 else ""} |')
    other = collections.Counter()
    for x in passed:
        for f in x['findings']:
            if f['gate'] != 'sync-check':
                other[(f['gate'], x['name'], f['detail'][:90])] += 1
    print('\n## Other gates (passed results)\n\n| gate | test | finding | count |\n|---|---|---|---|')
    for (g, n, d), c in sorted(other.items(), key=lambda kv: (kv[0][0], -kv[1]))[:80]:
        print(f'| {g} | {n} | {d} | {c} |')
    return rows


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('report')
    r.add_argument('dirs', nargs='*')
    r.add_argument('--json')
    s = sub.add_parser('scan')
    s.add_argument('logs', nargs='+')
    a = ap.parse_args(argv)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if a.cmd == 'report':
        if not a.dirs:
            import subprocess
            common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=root,
                                    capture_output=True, text=True).stdout.strip()
            main_root = os.path.dirname(common) if common else root
            a.dirs = lab_dirs(main_root)
        report(a.dirs, a.json)
        return 0
    if a.cmd == 'scan':
        for p in a.logs:
            for f in scan(timed(read_lines(p), os.path.getmtime(p)), os.path.basename(p)):
                print(f'{f["gate"]:10} {f["who"]}: {f["detail"]}')
        return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
