#!/usr/bin/env python3
"""Self-test of the test lab (tools/lab.py) and of the lane paths of the test harness, no game needed
(make test). Fake tests run through lab.cmd_run / lab.run_one with every lane action stubbed out
(no game, no real lane is touched: lane 10 with a fake game folder in a temporary directory).

  python3 tools/tests/lab_selftest.py [--tree <checkout>]
  --tree: test the lab of another checkout (e.g. main before the fixes: every case below FAILs there)

Cases (ticket):
  crash_after   RESULT: OK, then a crash report appears: FAIL (BUG-0036)
  died_after    RESULT: OK, then a game stops answering: FAIL (BUG-0036)
  faults        a non-zero fault count in the log: FAIL (BUG-0042)
  fails_once    RESULT: FAIL, OK on a retry: FAIL, not "ok (flaky)" (BUG-0073)
  harness_once  no RESULT line, OK on a retry: ok (flaky) (BUG-0073, the retry kept for harness trouble)
  skipper       RESULT: SKIP: skipped, not passed (BUG-0100)
  clean         RESULT: OK and nothing else: ok
  timeout       a test past the timeout leaves no process behind (BUG-0052)
  worktree      --branch fix/a never picks the worktree of fix/a-b (BUG-0048)
  game_log      twoinst.Monitor reads the lane's game log from a worktree too (BUG-0068)
  fixture       a fixture that lacks its facts fails Checks.faults() (BUG-0124)
  free_lane_*   a uses_free_lane test is watched on the free lane from its LAB-WATCH line: a crash or
                hang report there fails it, an older report does not (BUG-0249)
  owner_crash_lane1 / test_crash_lane1  on lane 1 a report of the owner's own Spore (run: instance 1,
                profile own) does not count, a test game's (profile set, instance 2, no run: line) does
  interrupt_*   Ctrl+C / SIGTERM in the exclusive phase: no test left, the run's lanes killed, exit
                non-zero; a retry does not start after it; the free lane is killed only when its lock
                is free; SIGHUP ignored stays ignored (BUG-0250)
  gate_*        the log gates (tools/loggates.py, docs/06-testing.md#testing-log-gates) over the lines the
                lane's games write during a test: a sync-check MISMATCH, a hitch over 2 s in play, the
                address space low or lost by the minute fail it; lines of an earlier test (a warm game's
                log), a forced mismatch inside LAB-MARK sync-force lines, an allow(...) of suite.py,
                SPORE_GATES=report, a game that started again (its whole new log is the test's) and
                a hitch of a load or of the frame source switch are handled
  kill_args     lab.py kill: --lanes 3 is refused as ambiguous, --lane / --upto / --lanes 2,3, lane 1 only
                with --include-lane-1, an empty selection exits 1 (BUG-0253)
"""
import argparse
import contextlib
import io
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

LANE = 10
FAILS = []


def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what, flush=True)
    if not cond:
        FAILS.append(what)


def git(cwd, *a):
    return subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', *a], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout


def leftovers(marker):
    """processes whose command line holds the marker (/proc, never pkill -f / ps | grep)"""
    found = []
    for pid in os.listdir('/proc'):
        if pid.isdigit():
            try:
                cmd = open(f'/proc/{pid}/cmdline', 'rb').read().replace(b'\0', b' ').decode(errors='replace')
            except OSError:
                continue
            if marker in cmd and int(pid) != os.getpid():
                found.append(int(pid))
    return found


def lab_cases(tree, tmp):
    sys.path.insert(0, os.path.join(tree, 'tools'))
    import lab
    import suite
    game = os.path.join(tmp, 'game')
    logs = os.path.join(game, 'SporebinEP1', 'mods', 'logs')
    os.makedirs(logs)
    alive = os.path.join(tmp, 'alive')
    git(tmp, 'init', '-q', '-b', 'main')  # the run's tree (its head line)
    git(tmp, 'commit', '-q', '--allow-empty', '-m', 'selftest')
    lab.ROOT = tmp  # out/lab/<run> and flaky.json go there
    lab.preflight = lambda: True
    lab.ensure_lanes = lambda n: None
    lab.spore = lambda *a, **k: subprocess.CompletedProcess(a, 0, '', '')
    lab.lane_game = lambda lane: game
    # both games of the fake lane answer while the file exists
    lab.lane_alive = lambda lane: {2 * lane - 1: os.path.exists(alive), 2 * lane: os.path.exists(alive)}
    lab.AFTER = 2.0
    py = f'{sys.executable} -c'
    cnt = os.path.join(tmp, 'cnt')
    suite.T[:] = []
    suite.add('clean', f'{py} "print(\'RESULT: OK\')"', ('smoke',))
    suite.add('crash_after', f'{py} "print(\'RESULT: OK\');open(\'{logs}/crash-20261028-000000-i19.txt\',\'w\')'
                             f'.write(\'crash\')"', ('smoke',))
    suite.add('died_after', f'touch {alive}; {py} "print(\'RESULT: OK\')"; (sleep 1; rm -f {alive}) &', ('smoke',))
    suite.add('faults', f'{py} "print(\'host faults: total faults: 3 (0xC0000005 at coop.dll+0x1234)\');'
                        f'print(\'RESULT: OK\')"', ('smoke',))
    suite.add('fails_once', f'{py} "import os;p=\'{cnt}1\';n=os.path.exists(p);open(p,\'a\').close();'
                            f'print(\'RESULT:\', \'OK\' if n else \'FAIL\');exit(0 if n else 1)"', ('smoke',))
    suite.add('harness_once', f'{py} "import os;p=\'{cnt}2\';n=os.path.exists(p);open(p,\'a\').close();'
                              f'print(\'RESULT: OK\' if n else \'the game did not answer\');exit(0 if n else 1)"',
              ('smoke',))
    suite.add('skipper', f'{py} "print(\'RESULT: SKIP (no free lane)\')"', ('smoke',))
    a = argparse.Namespace(branch=None, tests='smoke', repeat=1, skip=','.join(str(n) for n in range(1, LANE)),
                           lanes=1, no_build=True, cold=False, retry=1)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = lab.cmd_run(a)
    # the inner run's output only as data: under the lab (make test is the test unit) its fake fault and
    # result lines must not look like this test's own
    inner = out.getvalue().strip().replace('RESULT:', 'RESULT=').replace(' faults: ', ' faults= ')
    print('    ' + inner.replace('\n', '\n    '))
    run = sorted(os.listdir(os.path.join(tmp, 'out', 'lab')))
    run = [d for d in run if d != 'flaky.json'][-1]
    res = {r['name']: r for r in json.load(open(os.path.join(tmp, 'out', 'lab', run, 'results.json')))['results']}
    summary = open(os.path.join(tmp, 'out', 'lab', run, 'summary.md')).read()
    check(res['clean']['ok'] and not res['clean'].get('skipped'), 'clean: ok')
    check(not res['crash_after']['ok'], 'crash_after: a crash report after an OK result fails the test (BUG-0036)')
    check(not res['died_after']['ok'], 'died_after: a game that dies after an OK result fails the test (BUG-0036)')
    check(not res['faults']['ok'], 'fault count: a non-zero fault count fails the test (BUG-0042)')
    check(not res['fails_once']['ok'], 'fails_once: a FAIL result is not made a pass by a retry (BUG-0073)')
    check(res['harness_once']['ok'] and res['harness_once'].get('flaky'),
          'harness_once: no RESULT line, then OK on the retry: ok (flaky)')
    check(res['skipper'].get('skipped') and '| skipper | skip' in summary,
          'skipper: a SKIP result is a skip in results.json and summary.md, not a pass (BUG-0100)')
    check(rc != 0, 'the run fails')

    # BUG-0052: a test past the timeout (a command bash does not exec: a list) leaves nothing behind
    marker = f'labselftest_sleep_{os.getpid()}'
    lab.TIMEOUT = 2
    logdir = os.path.join(tmp, 'timeout')
    os.makedirs(logdir)
    t = dict(name='timeout', cmd=f'{py} "import time; time.sleep(30)" {marker}; true', warm=False, fixture=None,
             exclusive=False, minutes=1, reports=False)
    with contextlib.redirect_stdout(io.StringIO()):
        r = lab.run_one(t, LANE, tree, logdir, False)
    time.sleep(0.5)
    left = leftovers(marker)
    check(not r['ok'] and not left, f'timeout: the test failed and no process of it is left ({left})')
    for pid in left:
        try:
            os.kill(pid, 9)
        except OSError:
            pass


GATE_TEST = r'''
import sys, time
logs, case = sys.argv[1], sys.argv[2]
def tod(t):
    return time.strftime('%H:%M:%S', time.localtime(t)) + '.%03d' % int((t % 1) * 1000)
def w(who, text, t=None, mode='a'):
    with open(f'{logs}/sporekit.{19 if who == "host" else 20}.log', mode) as f:
        f.write(f'{tod(t or time.time())} [s 1.0] [INF] coop: {text}\n')
MM = "sync-check: MISMATCH with player 1 'Bob' (Tribe A3930F03, 2 checks 3 s apart, 37 objects compared): p: 1 apart (p1 22.3)"
if case == 'clean':
    w('host', 'sync-check: OK with player 1 (Tribe A3930F03, 37 objects compared)')
elif case in ('mismatch', 'allowed', 'report', 'nogame'):
    w('guest', MM.replace("player 1 'Bob'", 'the host'))
elif case == 'forced':
    print('LAB-MARK sync-force on %.3f' % time.time(), flush=True)
    w('host', MM)
    print('LAB-MARK sync-force off %.3f' % time.time(), flush=True)
elif case == 'restart':  # the game started again: a new log, its MISMATCH is the test's
    w('host', 'sporekit: SporeKit 0.3.5 booting (instance 19, pid 7)', mode='w')
    w('host', MM)
elif case == 'hitch':
    w('host', 'perf: hitch 3500 ms (Creature)')
elif case == 'hitch_ok':
    w('host', 'perf: hitch 3500 ms (Creature, loading)')
    w('host', 'perf: hitch 2008 ms (Creature)')  # SporeKit's switch to game loop frames (2.0 s)
    w('host', 'perf: hitch 1500 ms (Creature)')
elif case == 'monitor':
    print('MONITOR-VIOLATION puppet guest: guest: partner 0 (Alice) in the same place is not shown', flush=True)
elif case == 'memlow':
    w('host', "mem: the game's address space runs low: 250 MB free, largest free block 40 MB (a crash ...)")
elif case in ('slope', 'slope_ok'):
    now = time.time()
    loss = 120 if case == 'slope' else 2
    for k in range(7):
        w('host', f'perf: fps avg 60 min 55 (1 s windows), longest frame 30 ms, hitches 0 | address space '
                  f'{3500 - loss * k} of 4095 MB free, largest free block 2000 MB | Cell', t=now - 360 + 60 * k)
print('RESULT: OK')
'''


def gate_cases(tree, tmp):
    """the log gates in lab.run_one: a fake lane whose game logs the fake test writes"""
    sys.path.insert(0, os.path.join(tree, 'tools'))
    import lab
    import suite
    games = os.path.join(tmp, 'gates')
    logs = os.path.join(games, 'SporebinEP1', 'mods', 'logs')
    os.makedirs(logs)
    lab.lane_game = lambda lane: games
    lab.lane_alive = lambda lane: {2 * lane - 1: False, 2 * lane: False}
    lab.preflight = lambda: True
    script = os.path.join(tmp, 'gate_test.py')
    open(script, 'w').write(GATE_TEST)
    logdir = os.path.join(tmp, 'gatelogs')
    os.makedirs(logdir)
    if not hasattr(lab, 'log_gates'):
        check(False, 'gate: the lab has no log gates')
        return

    def one(case, mode='fail', **kw):
        # an earlier test's MISMATCH in both warm games' logs: never this test's
        for inst in (19, 20):
            with open(os.path.join(logs, f'sporekit.{inst}.log'), 'w') as f:
                f.write(f'{time.strftime("%H:%M:%S")}.000 [INF] sporekit: SporeKit 0.3.5 booting (instance {inst}, pid 5)\n'
                        f'{time.strftime("%H:%M:%S")}.100 [WRN] coop: sync-check: MISMATCH with the host (Cell 1234ABCD, '
                        f'3 objects compared): c: 3 missing on the guest (c1 c2 c3)\n')
        t = dict(name=f'gate_{case}', cmd=f'{sys.executable} {script} {logs} {case}', warm=False, fixture=None,
                 exclusive=False, minutes=1, reports=False, **kw)
        lab.GATES_MODE = mode
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                return lab.run_one(t, LANE, tree, logdir, False)
        finally:
            lab.GATES_MODE = 'fail'
    r = one('clean')
    check(r['ok'] and not r['gates'], f'gate_clean: an earlier test\'s MISMATCH in a warm log does not count ({r["fails"]})')
    r = one('mismatch')
    check(not r['ok'] and any('gate sync-check' in x for x in r['fails']),
          f'gate_mismatch: a sync-check MISMATCH during the test fails it ({r["fails"][:1]})')
    r = one('allowed', allow=[suite.allow('sync-check', 'BUG-0001', 'selftest', stage='Tribe', kind='p apart')])
    check(r['ok'] and any('allowed' in x and 'BUG-0001' in x for x in r['gates']),
          f'gate_allowed: an allow(...) with its ticket passes and says so ({r["gates"][:1]})')
    r = one('allowed', allow=[suite.allow('sync-check', 'BUG-0001', 'selftest', stage='Cell')])
    check(not r['ok'], 'gate_allowed: an allow of another stage does not cover it')
    r = one('report', mode='report')
    check(r['ok'] and any('report only' in x for x in r['gates']), f'gate_report: SPORE_GATES=report says it, passes ({r["gates"][:1]})')
    r = one('nogame', gates=suite.NO_GAME)
    check(r['ok'], 'gate_nogame: NO_GAME tests are not judged by the lane\'s idle games')
    r = one('forced')
    check(r['ok'], f'gate_forced: a MISMATCH inside LAB-MARK sync-force on/off is left out ({r["fails"][:1]})')
    r = one('restart')
    check(not r['ok'] and any('gate sync-check' in x for x in r['fails']),
          'gate_restart: a game started again during the test: its new log is judged whole')
    r = one('hitch')
    check(not r['ok'] and any('gate hitch' in x for x in r['fails']), f'gate_hitch: a 3.5 s hitch in play fails ({r["fails"][:1]})')
    r = one('hitch_ok')
    check(r['ok'], f'gate_hitch: a hitch while loading, the 2.0 s frame source switch and 1.5 s do not ({r["fails"][:1]})')
    r = one('hitch', gates={'hitch_ms': 5000})
    check(r['ok'], 'gate_hitch: a test\'s own threshold (gates={"hitch_ms": 5000})')
    r = one('monitor')
    check(not r['ok'] and any('gate monitor' in x for x in r['fails']), 'gate_monitor: a MONITOR-VIOLATION line fails the test')
    r = one('monitor', allow=[suite.allow('monitor', 'BUG-0001', 'selftest', kind='puppet')])
    check(r['ok'] and any('allowed gate monitor' in x for x in r['gates']), 'gate_monitor: allowed by its invariant')
    r = one('memlow')
    check(not r['ok'] and any('gate mem-low' in x for x in r['fails']), 'gate_memlow: "address space runs low" fails')
    r = one('slope')
    check(not r['ok'] and any('gate as-slope' in x for x in r['fails']),
          f'gate_slope: 120 MB/min of address space lost over 6 minutes fails ({r["fails"][:1]})')
    r = one('slope_ok')
    check(r['ok'], f'gate_slope: 2 MB/min does not ({r["fails"][:1]})')


def worktree_case(tree, tmp):
    """lab.worktree() of a branch whose name is a prefix of another checked-out branch; the tree's
    lab.py runs from a copy inside a scratch repository (it never sees the real one)"""
    repo = os.path.join(tmp, 'wt', 'SporeCoop')
    for f in ('tools/lab.py', 'tools/loggates.py', 'tools/tests/suite.py'):
        os.makedirs(os.path.dirname(os.path.join(repo, f)), exist_ok=True)
        shutil.copy(os.path.join(tree, f), os.path.join(repo, f))
    git(repo, 'init', '-q', '-b', 'main')
    git(repo, 'add', '-A')
    git(repo, 'commit', '-q', '-m', 'x')
    git(repo, 'branch', 'fix/a')
    other = os.path.join(tmp, 'wt', 'SporeCoop-wt', 'fix-a-b')
    git(repo, 'worktree', 'add', '-q', '-b', 'fix/a-b', other)
    r = subprocess.run([sys.executable, '-c', 'import sys; sys.path.insert(0, "tools"); import lab; '
                        'print(lab.worktree("fix/a"))'], cwd=repo, capture_output=True, text=True)
    got = r.stdout.strip() or r.stderr.strip()[-200:]
    check(os.path.isdir(got) and os.path.realpath(got) != os.path.realpath(other) and
          git(got, 'branch', '--show-current').strip() == 'fix/a',
          f'worktree: fix/a -> {os.path.relpath(got, tmp) if got.startswith("/") else got}, '
          'not the tree of fix/a-b (BUG-0048)')


def game_log_case(tree, tmp):
    """the harness files in a fake layout <tmp>/gl/SPORE/SporeCoop + a worktree, run from the worktree"""
    base = os.path.join(tmp, 'gl')
    repo = os.path.join(base, 'SPORE', 'SporeCoop')
    for f in ('tools/sporectl.py', 'tools/tests/lane.py', 'tools/tests/twoinst.py', 'tools/tests/fixtures.py'):
        os.makedirs(os.path.dirname(os.path.join(repo, f)), exist_ok=True)
        shutil.copy(os.path.join(tree, f), os.path.join(repo, f))
    git(repo, 'init', '-q', '-b', 'main')
    git(repo, 'add', '-A')
    git(repo, 'commit', '-q', '-m', 'x')
    wt = os.path.join(base, 'SPORE', 'SporeCoop-wt', 'fix', 'x')
    git(repo, 'worktree', 'add', '-q', '-b', 'fix/x', wt)
    want = {1: os.path.join(base, 'SPORE', 'SporebinEP1', 'mods', 'logs', 'sporekit.log'),
            3: os.path.join(base, 'SPORE-lab', 'lane2', 'SporebinEP1', 'mods', 'logs', 'sporekit.3.log')}
    for where in (repo, wt):
        for inst, path in want.items():
            lane = (inst + 1) // 2
            r = subprocess.run([sys.executable, '-c', 'import sys; sys.path.insert(0, "tools/tests"); import twoinst; '
                                f'print(twoinst.Monitor({inst}, "host").game_log())'], cwd=where,
                               env=dict(os.environ, SPORE_LANE=str(lane)), capture_output=True, text=True)
            got = r.stdout.strip() or r.stderr.strip()[-200:]
            check(got == path, f'game_log: instance {inst} from {os.path.relpath(where, base)} -> '
                               f'{os.path.relpath(got, base) if got.startswith("/") else got} (BUG-0068)')
    # BUG-0124: a fixture without its facts fails Checks.faults() (a fake console that answers nothing)
    code = ('import sys; sys.path.insert(0, "tools/tests"); import twoinst\n'
            'class C:\n'
            '    def call(self, line):\n'
            '        return {"ok": True, "out": "total faults: 0" if line == "exc" else ""}\n'
            'twoinst.check_fixture(C(), "tribe")\n'
            'ck = twoinst.Checks()\n'
            'ck.faults(host=C())\n'
            'print("checks ok" if ck.ok else "checks failed")\n')
    r = subprocess.run([sys.executable, '-c', code], cwd=wt, env=dict(os.environ, SPORE_LANE='2'),
                       capture_output=True, text=True)
    check('checks failed' in r.stdout, 'fixture: a fixture that lacks its facts fails Checks.faults() (BUG-0124): '
          + (r.stdout.strip().splitlines() or [r.stderr.strip()[-200:]])[-1])


def watch_cases(tree, tmp):
    """BUG-0249: the free lane of a uses_free_lane test, and lane 1's owner reports"""
    sys.path.insert(0, os.path.join(tree, 'tools'))
    import lab
    games = os.path.join(tmp, 'watch')
    lab.lane_game = lambda lane: os.path.join(games, f'lane{lane}')
    lab.lane_alive = lambda lane: {2 * lane - 1: False, 2 * lane: False}
    lab.spore = lambda *a, **k: subprocess.CompletedProcess(a, 0, '', '')
    lab.preflight = lambda: True
    lab.AFTER = 0.1
    logs = lambda lane: os.path.join(games, f'lane{lane}', 'SporebinEP1', 'mods', 'logs')  # noqa: E731
    for lane in (1, 9, LANE):
        os.makedirs(logs(lane), exist_ok=True)
    logdir = os.path.join(tmp, 'watchlogs')
    os.makedirs(logdir, exist_ok=True)
    py = f'{sys.executable} -c'

    def one(name, body, lane=LANE, free=True):
        t = dict(name=name, cmd=f'{py} "{body}"', warm=False, fixture=None, exclusive=True, minutes=1,
                 reports=False, uses_free_lane=free)
        with contextlib.redirect_stdout(io.StringIO()):
            return lab.run_one(t, lane, tree, logdir, False)
    w = f"import time;print('LAB-WATCH lane=9 since=%.3f' % time.time());"
    r = one('free_lane_crash', w + f"open('{logs(9)}/crash-20261028-000001.txt','w').write('x');print('RESULT: OK')")
    check(not r['ok'], f'free_lane_crash: a crash report on the free lane after LAB-WATCH fails the test ({r["fails"][:1]})')
    old = os.path.join(logs(9), 'crash-20261028-000002.txt')
    open(old, 'w').write('x')
    os.utime(old, (time.time() - 600, time.time() - 600))
    for f in os.listdir(logs(9)):
        if f != os.path.basename(old):
            os.remove(os.path.join(logs(9), f))
    r = one('free_lane_old_report', w + "print('RESULT: OK')")
    check(r['ok'], f'free_lane_old_report: a report older than LAB-WATCH does not count ({r["fails"][:1]})')
    r = one('free_lane_hang', w + f"open('{logs(9)}/report-20261028-000003.txt','w').write('x');print('RESULT: OK')")
    check(not r['ok'], 'free_lane_hang: a hang report on the free lane fails the test')

    def lane1(name, run_line):
        body = (f"open('{logs(1)}/crash-20261028-1{abs(hash(name)) % 100000:05d}.txt','w').write('SporeCoop crash\\n{run_line}"
                f"\\n');print('RESULT: OK')")
        return one(name, body, lane=1, free=False)
    r = lane1('owner_crash_lane1', 'run: instance 1, profile own, pid 1')
    check(r['ok'], f'owner_crash_lane1: the owner\'s own Spore crashing during a lane-1 test does not fail it ({r["fails"][:1]})')
    for nm, line in (('test_crash_set', 'run: instance 1, profile set, pid 1'),
                     ('test_crash_inst2', 'run: instance 2, profile own, pid 1'), ('test_crash_noline', 'x')):
        r = lane1(nm, line)
        check(not r['ok'], f'test_crash_lane1 ({nm}): a test game\'s report on lane 1 fails the test')


INTERRUPT = r"""
import os, subprocess, sys, signal
tree, tmp, mode, rec = sys.argv[1:5]
sys.path.insert(0, os.path.join(tree, 'tools'))
import lab, suite, argparse
games = os.path.join(tmp, 'ilanes')
lab.ROOT = os.path.join(tmp, 'i_' + mode)
os.makedirs(lab.ROOT, exist_ok=True)
lab.preflight = lambda: True
lab.ensure_lanes = lambda n: None
lab.lane_game = lambda lane: os.path.join(games, f'lane{lane}')
lab.lane_alive = lambda lane: {}
def spore(tree, lane, *a, **k):
    open(rec, 'a').write(f'{lane} {" ".join(a)}' + chr(10))
    return subprocess.CompletedProcess(a, 0, '', '')
lab.spore = spore
marker = os.environ['MARK']
suite.T[:] = []
if mode == 'retry':
    suite.add('noresult', f'{sys.executable} -c "import time; time.sleep(30)" {marker}', ('smoke',))
else:
    try:
        suite.add('sleeper', f'{sys.executable} -c "import time; time.sleep(30)" {marker}', ('smoke',), exclusive=True,
                  uses_free_lane=True)
    except TypeError:  # a lab before BUG-0249 (--tree): the same test without the flag
        suite.add('sleeper', f'{sys.executable} -c "import time; time.sleep(30)" {marker}', ('smoke',), exclusive=True)
if mode == 'nohup':
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
a = argparse.Namespace(branch=None, tests='smoke', repeat=1, skip='1,2,3,4,7,8,9,10', lanes=1, no_build=True,
                       cold=False, retry=1)
sys.exit(lab.cmd_run(a))
"""


def interrupt_cases(tree, tmp):
    """BUG-0250: the lab in a child process, interrupted while a test runs"""
    script = os.path.join(tmp, 'interrupt_child.py')
    open(script, 'w').write(INTERRUPT)
    lanes = os.path.join(tmp, 'ilanes')
    os.makedirs(os.path.join(lanes, 'lane5'), exist_ok=True)
    subprocess.run(['git', 'init', '-q', '-b', 'main', tmp], capture_output=True)
    git(tmp, 'commit', '-q', '--allow-empty', '-m', 'selftest')

    def run(mode, sig, hold_lock=False):
        mark = f'labselftest_int_{mode}_{os.getpid()}'
        rec = os.path.join(tmp, f'rec_{mode}.txt')
        holder = None
        if hold_lock:
            import fcntl
            holder = open(os.path.join(lanes, '.update_check-lane6.lock'), 'a')
            fcntl.flock(holder, fcntl.LOCK_EX)
        child = subprocess.Popen([sys.executable, script, tree, tmp, mode, rec], env=dict(os.environ, MARK=mark),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(8 if mode != 'retry' else 7)  # the lab waits 5 s after starting a lane's worker
        child.send_signal(sig)
        try:
            out, _ = child.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            child.kill()
            out, _ = child.communicate()
        if holder:
            holder.close()
        time.sleep(0.5)
        left = leftovers(mark)
        for pid in left:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
        kills = open(rec).read().split('\n') if os.path.exists(rec) else []
        ldir = os.path.join(tmp, 'i_' + mode, 'out', 'lab')
        runs = [os.path.join(ldir, d) for d in os.listdir(ldir) if d != 'flaky.json'] if os.path.isdir(ldir) else []
        out = out.decode(errors='replace')
        if os.environ.get('LABSELFTEST_DEBUG'):
            print(out)
        return child.returncode, left, kills, out, runs
    rc, left, kills, out, _ = run('exclusive', signal.SIGINT)
    check(not left and '5 kill' in kills and rc != 0,
          f'interrupt_exclusive: Ctrl+C ends the test, kills lane 5\'s games, exits non-zero ({rc}, left {left}, {kills[-3:]})')
    check('6 kill' in kills, 'interrupt_exclusive: the free lane is cleaned when nobody holds its lock')
    rc, left, kills, out, _ = run('sigterm', signal.SIGTERM)
    check(not left and '5 kill' in kills and rc != 0, f'interrupt_sigterm: SIGTERM as Ctrl+C ({rc}, left {left})')
    rc, left, kills, out, _ = run('locked', signal.SIGINT, hold_lock=True)
    check('6 kill' not in kills and 'in use by another run' in out,
          f'free_lane_locked: the free lane is left alone while another run holds its lock ({kills[-3:]})')
    rc, left, kills, out, runs = run('retry', signal.SIGINT)
    logs = [f for d in runs for f in os.listdir(d) if '.retry' in f]
    check(not left and not logs, f'interrupt_retry: no .retry test starts after Ctrl+C ({logs}, left {left})')
    # nohup: a lab whose SIGHUP is ignored keeps running through a hangup (no SIGHUP handler of ours)
    mark = f'labselftest_int_nohup_{os.getpid()}'
    child = subprocess.Popen([sys.executable, script, tree, tmp, 'nohup', os.path.join(tmp, 'rec_nohup.txt')],
                             env=dict(os.environ, MARK=mark), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(8)
    child.send_signal(signal.SIGHUP)
    time.sleep(1)
    alive = child.poll() is None
    child.send_signal(signal.SIGINT)
    try:
        child.wait(timeout=60)
    except subprocess.TimeoutExpired:
        child.kill()
    for pid in leftovers(mark):
        os.kill(pid, 9)
    check(alive, 'nohup: a lab started with SIGHUP ignored survives a hangup')


def kill_case(tree, tmp):
    """lab.py kill's lane selection, with the stubbed spore recording its calls (BUG-0253)"""
    sys.path.insert(0, os.path.join(tree, 'tools'))
    import lab
    calls = []
    lab.spore = lambda root, lane, *a, **k: calls.append(lane) or subprocess.CompletedProcess(a, 0, '', '')

    def kill(*args):
        calls.clear()
        old = sys.argv
        sys.argv = ['lab.py', 'kill', *args]
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                rc = lab.main()
        except SystemExit as e:
            rc = e.code
        finally:
            sys.argv = old
        return rc, list(calls), out.getvalue()
    rc, c, out = kill('--lanes', '3')
    check(rc == 2 and c == [] and 'ambiguous' in out, f'kill --lanes 3: refused as ambiguous, no kill ({rc}, {c})')
    rc, c, _ = kill('--lane', '3')
    check(rc == 0 and c == [3], f'kill --lane 3: lane 3 ({c})')
    rc, c, _ = kill('--upto', '3')
    check(rc == 0 and c == [2, 3], f'kill --upto 3: lanes 2, 3 ({c})')
    rc, c, _ = kill('--upto', '3', '--include-lane-1')
    check(rc == 0 and c == [1, 2, 3], f'kill --upto 3 --include-lane-1: lanes 1..3 ({c})')
    rc, c, out = kill('--lanes', '2,3')
    check(rc == 0 and c == [2, 3] and 'lanes 2, 3' in out, f'kill --lanes 2,3: lanes 2, 3, printed ({c})')
    rc, c, out = kill('--lanes', '1,')
    check(rc == 1 and c == [] and 'lane 1 left out' in out, f'kill --lanes 1,: nothing, exit 1 with the reason ({rc}, {c})')
    rc, c, _ = kill()
    check(rc == 0 and c == list(range(2, 11)), f'kill: lanes 2..10 ({c})')
    rc, c, _ = kill('--lane', '11')
    check(rc not in (0, None) and c == [], f'kill --lane 11: refused ({rc}, {c})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tree', default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    a = ap.parse_args()
    tree = os.path.abspath(a.tree)
    print(f'lab self-test of {tree}', flush=True)
    with tempfile.TemporaryDirectory(prefix='labselftest-') as tmp:
        lab_cases(tree, tmp)
        worktree_case(tree, tmp)
        game_log_case(tree, tmp)
        kill_case(tree, tmp)
        gate_cases(tree, tmp)
        watch_cases(tree, tmp)
        interrupt_cases(tree, tmp)
    print('RESULT:', 'OK' if not FAILS else f'FAIL ({len(FAILS)})')
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
