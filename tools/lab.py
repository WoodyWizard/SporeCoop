#!/usr/bin/env python3
"""Test lab: runs the automated tests on several lanes at once (docs/10-dev-pipeline.md).

  lab.py lanes 3                       create lanes 2..3 (lane 1 = the real install)
  lab.py run smoke --lanes 3           build, then the smoke suite on lanes 1..3
  lab.py run full --lanes 3 --branch feat/x   the same for a branch (its own worktree + build)
  lab.py run tribe_play,select         chosen tests (names: tools/tests/suite.py)
  lab.py reload --lane 2               build, deploy and hot-reload coop.dll in the lane's running games
  lab.py run tribe_play --repeat 6 --retry 0   hunt a flaky bug: 6 runs, no retries
  lab.py kill [--lane N ... | --upto N | --lanes 2,3] [--include-lane-1]
                                       stop the games of lanes 2..10, of lane N, of lanes 2..N or of a list
                                       (lane 1: only with --include-lane-1, its test games only)

A lane runs one test at a time. Warm tests reuse the lane's running games (SPORE_WARM=1,
twoinst.start: ~10 s instead of ~40 s); after a failure the lane restarts cold. A test that dies
without a RESULT line (harness trouble: start, timeout) is retried once cold and marked flaky
when it passes then; a test that printed RESULT: FAIL is a failure, never retried (BUG-0073).
A test also fails when a game of its lane wrote a crash / hang report while it ran or in the
seconds after it, a game died right after it, or its log shows a non-zero fault count
(BUG-0036, BUG-0042; suite.py reports=True for tests that make them on purpose). RESULT: SKIP is
a skip, not a pass (BUG-0100). A timeout kills the test's whole process group (BUG-0052).
Exclusive tests run at the end, alone. Results: out/lab/<run>/ (one log per test, results.json,
summary.md).
"""
import argparse
import datetime
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'tests'))
import suite  # noqa: E402

TIMEOUT = 1800
AFTER = 5.0  # seconds after a test in which a game of its lane may still crash or die (BUG-0036)
REPORTS = ('crash-*.txt', 'crash-*.dmp', 'report-*.txt')  # SporeKit's crash / hang reports (sporekit/diag.cpp)


def git(*a, cwd=None):
    return subprocess.run(['git', *a], cwd=cwd or ROOT, capture_output=True, text=True, check=True).stdout.strip()


def worktree(branch):
    """the checkout of a branch: the main one if it is checked out there, else a worktree next to it.
    The branch line of a worktree must match exactly: fix/cell is not fix/cell-meat (BUG-0048)"""
    if git('branch', '--show-current') == branch:
        return ROOT
    for block in git('worktree', 'list', '--porcelain').split('\n\n'):
        lines = block.split('\n')
        if f'branch refs/heads/{branch}' in lines:
            return lines[0].split(' ', 1)[1]
    path = os.path.join(os.path.dirname(ROOT), 'SporeCoop-wt', branch.replace('/', '-'))
    git('worktree', 'add', path, branch)
    return path


def spore(tree, lane, *a, check=False):
    env = dict(os.environ, SPORE_LANE=str(lane))
    return subprocess.run([os.path.join(tree, 'tools', 'spore.sh'), *a], env=env, capture_output=True, text=True,
                          check=check)


def build(tree):
    r = subprocess.run(['make', '-j16'], cwd=tree, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit('build failed')


def ensure_lanes(n):
    for lane in range(2, n + 1):
        print(spore(ROOT, 1, 'lane', 'create', str(lane), check=True).stdout.strip())


def lane_game(lane):
    main = os.path.dirname(os.path.dirname(git('rev-parse', '--path-format=absolute', '--git-common-dir')))
    return main if lane == 1 else os.path.join(os.path.dirname(main), 'SPORE-lab', f'lane{lane}')


def save_game_logs(lane, logdir, name):
    """the games' own logs (SporeKit + mods) next to the test log: the next start overwrites them"""
    logs = os.path.join(lane_game(lane), 'SporebinEP1', 'mods', 'logs')
    for inst, who in ((2 * lane - 1, 'host'), (2 * lane, 'guest')):
        src = os.path.join(logs, 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')
        if os.path.exists(src):
            shutil.copy(src, os.path.join(logdir, f'{name}.{who}.game.log'))


def preflight():
    """the machine can start games: the Flatpak document portal (its FUSE mount) is needed by every
    Bottles launch; it vanished once for minutes (2026-10-22) and every start failed"""
    doc = f'/run/user/{os.getuid()}/doc/by-app/com.usebottles.bottles'
    if os.path.isdir(doc):
        return True
    print('  (environment: the document portal is gone - restarting it)', flush=True)
    subprocess.run(['systemctl', '--user', 'restart', 'xdg-document-portal'], capture_output=True)
    for _ in range(20):
        time.sleep(0.5)
        if os.path.isdir(doc):
            return True
    return False


def report_files(lane):
    """SporeKit's crash and hang reports in the lane's log folder"""
    import glob
    logs = os.path.join(lane_game(lane), 'SporebinEP1', 'mods', 'logs')
    return {p for pat in REPORTS for p in glob.glob(os.path.join(logs, pat))}


def lane_alive(lane):
    """{instance: answers its console} for the two games of a lane"""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    from sporectl import Console
    alive = {}
    for inst in (2 * lane - 1, 2 * lane):
        try:
            alive[inst] = bool(Console(inst, timeout=5).call('ping')['ok'])
        except (OSError, ValueError):
            alive[inst] = False
    return alive


_procs, _procs_lock = set(), threading.Lock()  # process groups of the running tests (Ctrl+C kills them)


def kill_group(p):
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def run_cmd(cmd, tree, env, f, timeout):
    """the test in its own session: a timeout kills its whole process group, not only /bin/sh
    (dash does not exec the command, the test would go on driving the lane: BUG-0052)"""
    p = subprocess.Popen(cmd, shell=True, cwd=tree, env=env, stdout=f, stderr=subprocess.STDOUT,
                         start_new_session=True)
    with _procs_lock:
        _procs.add(p)
    try:
        return p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_group(p)
        p.wait()
        f.write('\nLAB: TIMEOUT\n')
        return -9
    finally:
        with _procs_lock:
            _procs.discard(p)


def fault_count(line):
    m = re.search(r'\bfaults: total faults: (\d+)', line)
    return int(m.group(1)) if m else 0


def run_one(t, lane, tree, logdir, warm, suffix=''):
    preflight()
    env = dict(os.environ, SPORE_LANE=str(lane), SPORE_WARM='1' if warm else '0', PYTHONUNBUFFERED='1')
    log = os.path.join(logdir, t['name'] + suffix + '.log')
    t0 = time.time()
    reports0 = report_files(lane)
    with open(log, 'w') as f:
        f.write(f'# {t["cmd"]}  (lane {lane}, {"warm" if warm else "cold"})\n')
        f.flush()
        rc = run_cmd(t['cmd'], tree, env, f, TIMEOUT)
    # a crash after the checks is still a crash (CLAUDE.md 3, BUG-0036): a game that answered at the
    # end must still answer a few seconds later, and no new crash / hang report may appear
    alive0 = lane_alive(lane)
    time.sleep(AFTER if any(alive0.values()) else 1.0)
    alive1 = lane_alive(lane) if any(alive0.values()) else alive0
    died = [i for i, up in alive0.items() if up and not alive1.get(i)]
    new_reports = sorted(report_files(lane) - reports0)
    save_game_logs(lane, logdir, t['name'] + suffix)
    for p in new_reports:  # the next start of the lane may prune them
        try:
            shutil.copy(p, os.path.join(logdir, f'{t["name"]}{suffix}.{os.path.basename(p)}'))
        except OSError:
            pass
    text = open(log, errors='replace').read().split('\n', 1)[-1]  # the test's output, not the '# cmd' line
    has_result = 'RESULT:' in text or t['cmd'].startswith('make')
    skipped = 'RESULT: SKIP' in text and 'RESULT: FAIL' not in text
    faults = [l.strip() for l in text.splitlines() if ' faults: ' in l and 'total faults: 0' not in l]
    lab_fails = []
    if not t.get('reports'):
        lab_fails += [f'FAIL lab: instance {i} stopped answering after the test (crash after the checks?)'
                      for i in died]
        lab_fails += [f'FAIL lab: new report {os.path.basename(p)}' for p in new_reports]
        lab_fails += [f'FAIL lab: faults in the log: {l}' for l in faults if fault_count(l) > 0]
    ok = rc == 0 and 'RESULT: FAIL' not in text and 'Traceback' not in text and has_result and not lab_fails
    # the machine, not the mod: the game did not even start
    env = not ok and ('did not start' in text or "bwrap: Can't find" in text)
    m = re.search(r'\((both games ready|warm reset) in (\d+) s', text)
    return dict(name=t['name'], lane=lane, ok=ok, rc=rc, env=env, skipped=skipped and ok,
                harness=(not has_result or 'Traceback' in text) and not lab_fails,
                seconds=round(time.time() - t0), start=f'{m.group(1)} {m.group(2)} s' if m else '',
                fails=(lab_fails + [l.strip() for l in text.splitlines() if l.strip().startswith('FAIL')])[:8],
                faults=faults, reports=[os.path.basename(p) for p in new_reports], died=died, log=log)


def cmd_run(a):
    tree = worktree(a.branch) if a.branch else ROOT
    tests = suite.select(a.tests)
    if a.repeat > 1:  # hunting a flaky bug: the same tests many times
        tests = [dict(t, name=f'{t["name"]}#{k + 1}') for k in range(a.repeat) for t in tests]
    run_id = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + ('-' + a.branch.replace('/', '-') if a.branch else '')
    logdir = os.path.join(ROOT, 'out', 'lab', run_id)
    os.makedirs(logdir, exist_ok=True)
    head = git('log', '-1', '--format=%h %s', cwd=tree)
    tree_branch = git('branch', '--show-current', cwd=tree) or '(detached)'
    if a.branch and tree_branch != a.branch:  # never test another branch's tree under this name (BUG-0048)
        sys.exit(f'lab: the tree {tree} has {tree_branch} checked out, not {a.branch}')
    skip = {int(x) for x in a.skip.split(',') if x.strip()}
    # more than 6 lanes (12 games) on one GPU ended in a full GPU reset that took the desktop down
    # (docs/06-testing.md 6.19): SPORE_MAX_LANES raises the cap on purpose
    cap = int(os.environ.get('SPORE_MAX_LANES', '6'))
    if a.lanes > cap:
        print(f'lab: {a.lanes} lanes capped to {cap} (GPU load, docs/06-testing.md 6.19; SPORE_MAX_LANES)')
        a.lanes = cap
    a.lane_ids = [n for n in range(1, 11) if n not in skip][:a.lanes]
    # a lane for tests that take a whole lane of their own outside the run (update_check.py wipes
    # the mod from its lane): neither used by this run nor by other work (--skip); '' = none
    os.environ['SPORE_FREE_LANE'] = str(next((n for n in range(2, 9) if n not in a.lane_ids and n not in skip), ''))
    print(f'lab run {run_id}: {len(tests)} tests on {a.lanes} lane(s), tree {tree} [{tree_branch}] ({head})')
    if not a.no_build:
        build(tree)
    ensure_lanes(max(a.lane_ids))
    for lane in a.lane_ids:  # a run starts from clean games
        spore(tree, lane, 'kill')
    parallel = sorted([t for t in tests if not t['exclusive']], key=lambda t: -t['minutes'])
    alone = [t for t in tests if t['exclusive']]
    pending = list(parallel)
    results, lock = [], threading.Lock()
    stopping = threading.Event()

    def take(last_fixture):
        """the next test for a lane: one on the save the lane has loaded (warm), else the longest"""
        with lock:
            if not pending or stopping.is_set():
                return None
            same = [t for t in pending if t['warm'] and last_fixture and t['fixture'] == last_fixture]
            t = same[0] if same else pending[0]
            pending.remove(t)
            return t

    def report(r):
        with lock:
            results.append(r)
            flag = 'SKIP' if r.get('skipped') else 'OK  ' if r['ok'] else 'FAIL'
            extra = ' (flaky)' if r.get('flaky') else ''
            shown = r['fails'] + [x for x in r['faults'] if not any(x in y for y in r['fails'])]
            print(f'  {flag} {r["name"]:<15} lane {r["lane"]}  {r["seconds"]:>4} s  {r["start"]}{extra}'
                  + ''.join(f'\n         {x}' for x in shown), flush=True)

    def attempt(t, lane, warm_ok):
        r = run_one(t, lane, tree, logdir, t['warm'] and warm_ok and not a.cold)
        if stopping.is_set():
            return r, False
        if not r['ok'] or not t['warm']:
            spore(tree, lane, 'kill')  # a clean slate after a failure or a test with its own setup
            warm_ok = False
        else:
            warm_ok = True
        for k in range(2):  # environment trouble (the game did not start): again, not counted as flaky
            if r['ok'] or not r.get('env'):
                break
            spore(tree, lane, 'kill')
            time.sleep(5)
            r = run_one(t, lane, tree, logdir, False, f'.env{k + 1}')
            r['env_retries'] = k + 1
        # harness trouble only (no RESULT line, a traceback, a timeout): once more on a clean lane, passing
        # = flaky. A RESULT: FAIL, a crash or a fault is a failure of the run (BUG-0073)
        if not r['ok'] and a.retry and r['harness'] and not r.get('env'):
            r2 = run_one(t, lane, tree, logdir, False, '.retry')
            r2['flaky'] = r2['ok']
            r2['first'] = r['fails'] or ['no RESULT (harness)']
            spore(tree, lane, 'kill')
            return r2, False
        return r, warm_ok

    def worker(lane):
        warm_ok, last = False, None  # every lane starts cold
        while True:
            t = take(last if warm_ok else None)
            if t is None:
                return
            r, warm_ok = attempt(t, lane, warm_ok)
            last = t['fixture']
            report(r)

    t0 = time.time()
    threads = [threading.Thread(target=worker, args=(lane,)) for lane in a.lane_ids]
    try:
        for th in threads:
            th.start()
            time.sleep(5)  # Bottles sandboxes starting at the same moment can race
        for th in threads:
            th.join()
    except KeyboardInterrupt:  # the tests run in their own sessions: Ctrl+C does not reach them
        stopping.set()
        with _procs_lock:
            for p in list(_procs):
                kill_group(p)
        raise
    if alone:  # the machine alone: no game of another lane may be visible (LAN discovery)
        for lane in a.lane_ids:
            spore(tree, lane, 'kill')
    # exclusive tests run on a lane of this run (never on one --skip reserved for other work); LAN
    # discovery (the UI test) only sees hosts of lanes 1..5
    ex_lane = next((n for n in a.lane_ids if n <= 5), None)
    for t in alone:
        if ex_lane is None:
            report(dict(name=t['name'], lane=0, ok=True, skipped=True, rc=0, seconds=0, start='', faults=[],
                        fails=[f'exclusive: needs one of lanes 1-5 in this run (lanes {a.lane_ids})']))
            continue
        report(attempt(t, ex_lane, False)[0])
    for lane in a.lane_ids:
        spore(tree, lane, 'kill')
    total = time.time() - t0
    ok = all(r['ok'] for r in results)
    passed = sum(r['ok'] and not r.get('skipped') for r in results)
    skipped = sum(bool(r.get('skipped')) for r in results)
    flaky = sum(bool(r.get('flaky')) for r in results)
    notes = ''.join([f', {skipped} skipped' if skipped else '', f', {flaky} flaky (harness)' if flaky else ''])
    # flaky history across runs: which tests fail now and then (fix the test or the bug behind it)
    hist_path = os.path.join(ROOT, 'out', 'lab', 'flaky.json')
    try:
        hist = json.load(open(hist_path))
    except (OSError, ValueError):
        hist = {}
    for r in results:
        h = hist.setdefault(r['name'], {'runs': 0, 'flaky': 0, 'failed': 0, 'last': []})
        h['runs'] += 1
        h['flaky'] += bool(r.get('flaky'))
        h['failed'] += not r['ok']
        if r.get('flaky') or not r['ok']:
            h['last'] = (h['last'] + [f'{run_id}: ' + '; '.join(r.get('first') or r['fails'])[:300]])[-5:]
    json.dump(hist, open(hist_path, 'w'), indent=1)
    json.dump(dict(run=run_id, tree=tree, branch=tree_branch, head=head, skipped=skipped, lanes=a.lanes, seconds=round(total), ok=ok, results=results),
              open(os.path.join(logdir, 'results.json'), 'w'), indent=1)
    lines = [f'# lab run {run_id}', '', f'{tree_branch} {head} - {len(results)} tests, {a.lanes} lanes, '
             f'{total / 60:.1f} min, ' + (f'**all passed**{notes}' if ok else f'**FAILED**{notes}'), '', '| test | result | lane | s | start |', '|---|---|---|---|---|']
    for r in sorted(results, key=lambda r: (r['ok'], r['name'])):
        res = 'skip' if r.get('skipped') else 'ok' if r['ok'] else 'FAIL'
        lines.append(f'| {r["name"]} | {res}{" (flaky)" if r.get("flaky") else ""} | {r["lane"]} '
                     f'| {r["seconds"]} | {r["start"]} |')
    shaky = sorted(((h['flaky'] + h['failed']) / h['runs'], n) for n, h in hist.items() if h['flaky'] + h['failed'])
    if shaky:
        lines += ['', '## Unstable tests (all runs)', ''] + [
            f'- {n}: {hist[n]["flaky"]} flaky, {hist[n]["failed"]} failed in {hist[n]["runs"]} runs; last: '
            f'{hist[n]["last"][-1] if hist[n]["last"] else ""}' for _, n in reversed(shaky)]
    open(os.path.join(logdir, 'summary.md'), 'w').write('\n'.join(lines) + '\n')
    print(f'{"PASSED" if ok else "FAILED"}: {passed}/{len(results)}{notes} in {total / 60:.1f} min '
          f'-> out/lab/{run_id}/summary.md')
    return 0 if ok else 1


def cmd_reload(a):
    """the dev loop: rebuild, copy the DLLs into each lane and hot-reload coop.dll in its games"""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    from sporectl import Console
    build(ROOT)
    for lane in a.lane or [1]:
        spore(ROOT, lane, 'deploy')
        for inst in (2 * lane - 1, 2 * lane):
            try:
                c = Console(inst, timeout=30)
                out = c.call('mod.reload coop')['out'].strip()
                c.call('sym.reload')
                print(f'  lane {lane} instance {inst}: {out[:80]}')
            except OSError:
                print(f'  lane {lane} instance {inst}: not running')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('tests', nargs='?', default='smoke', help='smoke | full | nightly | name,name')
    r.add_argument('--lanes', type=int, default=int(os.environ.get('SPORE_LANES', '3')))
    r.add_argument('--skip', default='', help='lanes other work uses, e.g. 3,6: the run takes the next free ones')
    r.add_argument('--branch')
    r.add_argument('--cold', action='store_true', help='no warm reuse of running games')
    r.add_argument('--no-build', action='store_true')
    r.add_argument('--retry', type=int, default=1)
    r.add_argument('--repeat', type=int, default=1, help='run every test N times (flaky bug hunt)')
    ln = sub.add_parser('lanes')
    ln.add_argument('n', type=int)
    rl = sub.add_parser('reload')
    rl.add_argument('--lane', type=int, action='append', help='lane(s) to reload (default 1)')
    k = sub.add_parser('kill', help='end the games of lanes 2..10 (lane 1: --include-lane-1, test games only)')
    k.add_argument('--lanes', default='', help='a list of lanes, e.g. 2,3 (one lane: 3, with the comma)')
    k.add_argument('--lane', type=int, action='append', help='one lane (repeatable)')
    k.add_argument('--upto', type=int, help='lanes 2..N')
    k.add_argument('--include-lane-1', action='store_true',
                   help='also lane 1 (the real install): its test games only, never the owner\'s own Spore')
    a = ap.parse_args()
    if a.cmd == 'run':
        return cmd_run(a)
    if a.cmd == 'lanes':
        ensure_lanes(a.n)
    if a.cmd == 'reload':
        cmd_reload(a)
    if a.cmd == 'kill':
        return cmd_kill(a)
    return 0


def kill_lanes(a):
    """the lanes `lab.py kill` ends, or (None, message, exit code). BUG-0103: lane 1 (the owner's
    install) only on request. BUG-0253: `--lanes N` meant lanes 1..N up to 0.7.2 and lane N in
    0.7.3.0; a bare number is refused instead of guessed (parallel runs use other lanes)."""
    v = a.lanes.strip()
    if v and ',' not in v:
        return None, (f'lab kill: "--lanes {v}" is ambiguous (0.7.2: lanes 1..{v}; 0.7.3.0: lane {v}). Use '
                      f'--lane {v} for one lane, --upto {v} for lanes 2..{v}, or --lanes 2,{v} for a list.'), 2
    try:
        lanes = [int(x) for x in v.split(',') if x.strip()] if v else []
    except ValueError:
        return None, f'lab kill: --lanes {v}: lane numbers separated by commas', 2
    lanes += a.lane or []
    if a.upto is not None:
        lanes += list(range(2, a.upto + 1)) + ([1] if a.include_lane_1 else [])
    if not (v or a.lane or a.upto is not None):
        lanes = list(range(2, 11)) + ([1] if a.include_lane_1 else [])
    bad = [n for n in lanes if not 1 <= n <= 10]
    if bad:
        return None, f'lab kill: lane {bad[0]} is not one of 1..10', 2
    asked1 = 1 in lanes
    lanes = sorted(set(n for n in lanes if n != 1 or a.include_lane_1))
    note = 'lane 1 left out: --include-lane-1 to end its test games' if asked1 and not a.include_lane_1 else ''
    if not lanes:
        return None, (note + '; ' if note else '') + 'lab kill: no lane selected', 1
    return lanes, note, 0


def cmd_kill(a):
    lanes, msg, rc = kill_lanes(a)
    if msg:
        print(msg)
    if lanes is None:
        return rc
    print('lab kill: lanes ' + ', '.join(map(str, lanes)))
    for lane in lanes:  # a lane that does not exist has no processes: nothing happens
        spore(ROOT, lane, 'kill')
    return 0


if __name__ == '__main__':
    sys.exit(main())
