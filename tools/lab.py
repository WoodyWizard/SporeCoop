#!/usr/bin/env python3
"""Test lab: runs the automated tests on several lanes at once (docs/10-dev-pipeline.md).

  lab.py lanes 3                       create lanes 2..3 (lane 1 = the real install)
  lab.py run smoke --lanes 3           build, then the smoke suite on lanes 1..3
  lab.py run full --lanes 3 --branch feat/x   the same for a branch (its own worktree + build)
  lab.py run tribe_play,select         chosen tests (names: tools/tests/suite.py)
  lab.py reload --lane 2               build, deploy and hot-reload coop.dll in the lane's running games
  lab.py run tribe_play --repeat 6 --retry 0   hunt a flaky bug: 6 runs, no retries
  lab.py run nightly --quick           intermediate runs: the long play scenarios shortened (suite.py quick=,
                                       docs/06-testing.md#testing-lab-speed); never for the release rule
  lab.py coverage [--days 7]           every suite test with its last run (main + worktrees), never-run ones
  lab.py coverage --commit <rev> --require smoke,full,multi   the release rule (docs/06-testing.md#testing-release-rule)
  lab.py fixcheck                      "SPORE_FIX_OFF=x fails" claims of suite.py against recorded FAIL runs
  lab.py gates [out/lab dirs]          the log gates over recorded results, report only (tools/loggates.py)
  lab.py kill [--lane N ... | --upto N | --lanes 2,3] [--include-lane-1]
                                       stop the games of lanes 2..10, of lane N, of lanes 2..N or of a list
                                       (lane 1: only with --include-lane-1, its test games only)

A lane runs one test at a time. Warm tests reuse the lane's running games (SPORE_WARM=1,
twoinst.start: ~10 s instead of ~40 s); after a failure the lane restarts cold. A test that dies
without a RESULT line (harness trouble: start, timeout) is retried once cold and marked flaky
when it passes then; a test that printed RESULT: FAIL is a failure, never retried (BUG-0073).
A test also fails when a game of its lane wrote a crash / hang report while it ran or in the
seconds after it, a game died right after it, or its log shows a non-zero fault count
(BUG-0036, BUG-0042; suite.py reports=True for tests that make them on purpose), or when the games'
own logs written during it break a log gate (tools/loggates.py: sync-check MISMATCH, a hitch over
2 s in play, address space running low or lost by the minute; suite.py gates= / allow=;
SPORE_GATES=report: report only, off: none). RESULT: SKIP is
a skip, not a pass (BUG-0100). A timeout kills the test's whole process group (BUG-0052).
Exclusive tests run at the end, alone. Results: out/lab/<run>/ (one log per test, results.json,
summary.md).
"""
import argparse
import datetime
import glob
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
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import suite  # noqa: E402
import loggates  # noqa: E402

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


def ensure_lanes(lanes):
    for lane in lanes:
        if lane < 2:
            continue
        print(spore(ROOT, 1, 'lane', 'create', str(lane), check=True).stdout.strip())


def lane_game(lane):
    main = os.path.dirname(os.path.dirname(git('rev-parse', '--path-format=absolute', '--git-common-dir')))
    return main if lane == 1 else os.path.join(os.path.dirname(main), 'SPORE-lab', f'lane{lane}')


def game_logs(lane):
    """{who: the game log} of a lane's instances (the extra guests of a 4-player test: 20+N, 30+N; BUG-0266)"""
    logs = os.path.join(lane_game(lane), 'SporebinEP1', 'mods', 'logs')
    return {who: os.path.join(logs, 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')
            for inst, who in ((2 * lane - 1, 'host'), (2 * lane, 'guest'), (20 + lane, 'extra1'), (30 + lane, 'extra2'))}


def test_logs(lane, since):
    """the game logs a test of the lane wrote: host and guest, an extra guest's only when written since
    the test started (2 s: the kernel's coarse file clock)"""
    return {who: src for who, src in game_logs(lane).items()
            if os.path.exists(src) and (who in ('host', 'guest') or os.path.getmtime(src) >= since - 2)}


def save_game_logs(lane, logdir, name, since=0.0):
    """the games' own logs (SporeKit + mods) next to the test log: the next start overwrites them"""
    for who, src in test_logs(lane, since).items():
        shutil.copy(src, os.path.join(logdir, f'{name}.{who}.game.log'))


GATES_MODE = os.environ.get('SPORE_GATES', 'fail')  # fail | report | off (docs/06-testing.md#testing-log-gates)


def log_gates(t, lane, text, marks, since):
    """(fails, notes, marks) of the log gates over the lines the lane's games wrote during the test:
    from the offsets taken at its start (a game started again during it: its whole new log)"""
    cfg = t.get('gates') or {}
    if GATES_MODE == 'off' or set(loggates.GATES) <= set(cfg.get('off', ())):
        return [], [], {}
    logs = test_logs(lane, since)
    try:
        fails, notes, _ = loggates.check_logs(logs, cfg, list(t.get('allow') or []) + suite.ALLOW_ALL, text,
                                              {w: marks.get(w) for w in logs})
    except Exception as e:  # noqa: BLE001 (a broken gate must not lose the run; it is said)
        return [], [f'log gates: error {e!r}'], marks
    if GATES_MODE == 'report':
        notes = [f'report only (SPORE_GATES=report): {f[len("FAIL lab: "):]}' for f in fails] + notes
        fails = []
    return fails, notes, marks


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


def test_game_report(lane, path):
    """does a new report count against the lane's test? Lane 1's folder is also the owner's own
    install: a report whose `run:` line says instance 1 without a test profile is the owner's Spore
    (BUG-0249). Without the line (an older SporeKit, a minidump) it counts: fail closed."""
    if lane != 1:
        return True
    if path.endswith('.dmp'):
        txt = path[:-4] + '.txt'
        if not os.path.exists(txt):
            return True
        path = txt
    try:
        text = open(path, errors='replace').read(64 << 10)
    except OSError:
        return True
    m = re.search(r'^run: instance (\d+), profile (set|own)', text, re.M)
    return not m or m.group(2) == 'set' or int(m.group(1)) > 1


def free_lane_lock(lane):
    """the BUG-0211 lane lock of update_check.py (the same path)"""
    return os.path.join(os.path.dirname(lane_game(lane)), f'.update_check-lane{lane}.lock')


def free_lane_watch(text):
    """(lane, since) of a uses_free_lane test's `LAB-WATCH lane=N since=T` line, or None"""
    m = re.search(r'^LAB-WATCH lane=(\d+) since=([\d.]+)', text, re.M)
    return (int(m.group(1)), float(m.group(2))) if m else None


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
_stopping = threading.Event()  # set by an interrupt: no new test starts (BUG-0250)


def kill_group(p):
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def run_cmd(cmd, tree, env, f, timeout, lane=0, mem=None):
    """the test in its own session: a timeout kills its whole process group, not only /bin/sh
    (dash does not exec the command, the test would go on driving the lane: BUG-0052)"""
    p = subprocess.Popen(cmd, shell=True, cwd=tree, env=env, stdout=f, stderr=subprocess.STDOUT,
                         start_new_session=True)
    with _procs_lock:
        _procs.add(p)
        if _stopping.is_set():  # an interrupt came while it started (BUG-0250)
            kill_group(p)
    ms = MemSampler(p, lane)  # BUG-0916
    ms.start()
    try:
        rc = p.wait(timeout=timeout)
        if ms.over:
            f.write(f'\nLAB: MEMORY {ms.over}\n')
        return rc
    except subprocess.TimeoutExpired:
        kill_group(p)
        p.wait()
        f.write('\nLAB: TIMEOUT\n')
        return -9
    except KeyboardInterrupt:  # Ctrl+C / SIGTERM while this thread waits (the exclusive phase, BUG-0250)
        kill_group(p)
        p.wait()
        raise
    finally:
        if mem is not None:
            mem.update(ms.result())
        else:
            ms.stop_ev.set()
        with _procs_lock:
            _procs.discard(p)


def fault_count(line):
    m = re.search(r'\bfaults: total faults: (\d+)', line)
    return int(m.group(1)) if m else 0


# BUG-0663 (docs/06-testing.md#testing-xwayland-saturation): every lane's games draw through the one Xwayland of the
# headless shell; over ~90 % of a core it is saturated and the games of the heaviest stage starve (2-9 fps, "the
# console stopped answering"), whatever the mod does. Each test samples that Xwayland's CPU every 10 s; a failed test
# whose busiest window reached XW_LOAD is marked "(load)" in the summary with the numbers (still a failure: rerun it on
# a quiet machine before reading it as the mod's). SPORE_LAB_XWAYLAND=0: no sampling.
XW_LOAD = 0.9
XW_STEP = 10.0


def headless_xwayland():
    """pid of the Xwayland that serves the headless shell (its auth file is under sporecoop-headless), or None"""
    try:
        out = subprocess.run(['pgrep', '-x', 'Xwayland'], capture_output=True, text=True).stdout.split()
    except OSError:
        return None
    for pid in out:
        try:
            if b'sporecoop-headless' in open(f'/proc/{pid}/cmdline', 'rb').read():
                return int(pid)
        except OSError:
            pass
    return None


def proc_ticks(pid):
    try:
        f = open(f'/proc/{pid}/stat').read().rsplit(')', 1)[1].split()
        return int(f[11]) + int(f[12])  # utime + stime
    except (OSError, IndexError, ValueError):
        return None


class XwSampler(threading.Thread):
    """the headless Xwayland's share of one core: the whole test and its busiest XW_STEP window"""
    def __init__(self):
        super().__init__(daemon=True)
        self.pid = headless_xwayland() if os.environ.get('SPORE_LAB_XWAYLAND', '1') != '0' else None
        self.hz = os.sysconf('SC_CLK_TCK')
        self.stop_ev = threading.Event()
        self.first = self.last = None
        self.peak = 0.0

    def run(self):
        if not self.pid:
            return
        t, k = time.time(), proc_ticks(self.pid)
        self.first = (t, k)
        while k is not None and not self.stop_ev.wait(XW_STEP):
            t2, k2 = time.time(), proc_ticks(self.pid)
            if k2 is None:
                break
            self.peak = max(self.peak, (k2 - k) / self.hz / max(t2 - t, 1e-3))
            t, k = t2, k2
            self.last = (t, k)

    def result(self):
        self.stop_ev.set()
        self.join(2)
        if not self.first or not self.last or self.first[1] is None:
            return None
        avg = (self.last[1] - self.first[1]) / self.hz / max(self.last[0] - self.first[0], 1e-3)
        return {'avg': round(avg, 2), 'peak': round(self.peak, 2)}


# BUG-0916 (docs/06-testing.md#testing-lab-memory): a test script (the python3 that drives the games) needs 25-150 MB.
# One that grows without bound (a paging loop that never ended: 17 GB in minutes, four of them took the machine's 62 GB)
# is killed at SCRIPT_RSS_MB and fails with a clear line; every result carries the script's and the lane's games' peak
# RSS and the lowest MemAvailable seen. SPORE_LAB_SCRIPT_RSS_MB=0: no limit (the peaks are still recorded).
SCRIPT_RSS_MB = float(os.environ.get('SPORE_LAB_SCRIPT_RSS_MB', '2048') or 0)
MEM_STEP = 2.0
# The memory gate: a test starts only when, with its games at their full size (MEM_GAME_GB each, suite.games(t) of
# them) and the games of the run's tests started in the last MEM_RAMP seconds grown to theirs, MemAvailable stays over
# MEM_FLOOR_GB. Otherwise it waits (other tests of the run end, other work frees memory); after MEM_WAIT seconds with
# no test of this run running it fails as not started. SPORE_LAB_MEM_FLOOR_GB=0: no gate.
# MEM_GAME_GB: measured 2026-11-11, a game of a 4-player campaign: 0.5 GB RSS in Cell, 0.8 GB in Space after 80 min;
# MemAvailable rose by 3.9 GB when the campaign's four games ended (the games, their Wine helpers, GPU buffers in RAM).
MEM_GAME_GB = float(os.environ.get('SPORE_LAB_GAME_GB', '1.0') or 0)
MEM_FLOOR_GB = float(os.environ.get('SPORE_LAB_MEM_FLOOR_GB', '8') or 0)
MEM_RAMP = 900.0
MEM_WAIT = float(os.environ.get('SPORE_LAB_MEM_WAIT', '1800') or 0)
MEM_POLL = 10.0


def mem_available_gb():
    try:
        with open('/proc/meminfo') as f:
            for ln in f:
                if ln.startswith('MemAvailable:'):
                    return int(ln.split()[1]) / 1048576
    except (OSError, ValueError, IndexError):
        pass
    return None


def _rss_kb(pid):
    try:
        with open(f'/proc/{pid}/statm') as f:
            return int(f.read().split()[1]) * (os.sysconf('SC_PAGE_SIZE') // 1024)
    except (OSError, ValueError, IndexError):
        return 0


def session_scripts(sid):
    """{pid: (rss kB, command)} of the python processes in the test's session (the test script and its helpers;
    the games are Wine processes, not counted here)"""
    out = {}
    for d in os.listdir('/proc'):
        if not d.isdigit():
            continue
        try:
            stat = open(f'/proc/{d}/stat').read()
            comm = stat[stat.index('(') + 1:stat.rindex(')')]
            if not comm.startswith('python') or int(stat.rsplit(')', 1)[1].split()[3]) != sid:
                continue
            cmd = open(f'/proc/{d}/cmdline', 'rb').read().replace(b'\0', b' ').decode('utf-8', 'replace').strip()
        except (OSError, ValueError, IndexError):
            continue
        out[int(d)] = (_rss_kb(d), cmd[:160])
    return out


def lane_games_rss(lane):
    """(count, RSS kB in all) of the SporeApp.exe processes whose working directory is the lane's game folder"""
    try:
        pids = subprocess.run(['pgrep', '-x', 'SporeApp.exe'], capture_output=True, text=True).stdout.split()
        game = os.path.realpath(lane_game(lane))
    except Exception:  # noqa: BLE001 (a number for the record, never the test's failure)
        return 0, 0
    n = kb = 0
    for pid in pids:
        try:
            cwd = os.readlink(f'/proc/{pid}/cwd')
        except OSError:
            continue
        if cwd == game or cwd.startswith(game + os.sep):
            n += 1
            kb += _rss_kb(pid)
    return n, kb


class MemSampler(threading.Thread):
    """the test script's memory while it runs: the peak RSS of a python process of its session, the peak of the
    lane's games, the lowest MemAvailable; over SCRIPT_RSS_MB the test's process group is killed (self.over)"""
    def __init__(self, proc, lane, limit_mb=None):
        super().__init__(daemon=True)
        self.proc, self.lane = proc, lane
        self.limit = SCRIPT_RSS_MB if limit_mb is None else limit_mb
        self.stop_ev = threading.Event()
        self.peak_kb, self.peak_cmd, self.over = 0, '', None
        self.games_kb, self.games_n, self.avail_min = 0, 0, None

    def sample(self):
        for pid, (kb, cmd) in session_scripts(self.proc.pid).items():
            if kb > self.peak_kb:
                self.peak_kb, self.peak_cmd = kb, cmd
        if self.limit and self.peak_kb > self.limit * 1024 and self.over is None:
            self.over = (f'the test script used {self.peak_kb // 1024} MB of memory (limit {self.limit:.0f} MB, '
                         f'SPORE_LAB_SCRIPT_RSS_MB): killed. Its python3: {self.peak_cmd}')
            kill_group(self.proc)
            return
        if self.lane:
            n, kb = lane_games_rss(self.lane)
            if kb > self.games_kb:
                self.games_kb, self.games_n = kb, n
        av = mem_available_gb()
        if av is not None:
            self.avail_min = av if self.avail_min is None else min(self.avail_min, av)

    def run(self):
        while not self.stop_ev.wait(MEM_STEP) and self.over is None:
            try:
                self.sample()
            except Exception:  # noqa: BLE001 (the sampler must not end a test)
                pass

    def result(self):
        self.stop_ev.set()
        self.join(5)
        return dict(script_rss_mb=self.peak_kb // 1024, games_rss_mb=self.games_kb // 1024, games=self.games_n,
                    avail_min_gb=None if self.avail_min is None else round(self.avail_min, 1), over=self.over)


class MemGate:
    """the run's memory gate (see MEM_GAME_GB above)"""
    def __init__(self, out=print):
        self.lock = threading.Lock()
        self.held = []  # [start time, GB] of the run's running tests
        self.out = out

    def pending(self, now):
        """GB the games of the tests started lately will still take (their share falls to 0 over MEM_RAMP)"""
        return sum(gb * max(0.0, 1.0 - (now - t0) / MEM_RAMP) for t0, gb in self.held)

    def admit(self, t, stopping=None):
        """a token to give back with release() once the test may start; None after an interrupt; a string (why)
        when it could not start"""
        need = suite.games(t) * MEM_GAME_GB
        if not MEM_FLOOR_GB or not need:
            return []
        t0, said = time.time(), False
        idle_since = None
        while True:
            with self.lock:
                av = mem_available_gb()
                now = time.time()
                pend = self.pending(now)
                if av is None or av - pend - need >= MEM_FLOOR_GB:
                    tok = [now, need]
                    self.held.append(tok)
                    if said:
                        self.out(f'  (memory: {t["name"]} starts after {now - t0:.0f} s: {av:.1f} GB available)')
                    return tok
                running = len(self.held)
            why = (f'{av:.1f} GB available, {pend:.1f} GB for the games of tests just started, {need:.1f} GB for its own '
                   f'{suite.games(t)} games, {MEM_FLOOR_GB:.0f} GB to stay free (docs/06-testing.md#testing-lab-memory)')
            if not said:
                said = True
                self.out(f'  (memory: {t["name"]} waits: {why})')
            idle_since = None if running else (idle_since or now)
            if idle_since is not None and now - idle_since >= MEM_WAIT:
                return f'FAIL lab: not started, too little memory after {now - t0:.0f} s: {why}'
            if stopping is not None and stopping.wait(MEM_POLL):
                return None
            if stopping is None:
                time.sleep(MEM_POLL)

    def release(self, tok):
        with self.lock:
            if tok in self.held:
                self.held.remove(tok)


def run_one(t, lane, tree, logdir, warm, suffix=''):
    preflight()
    env = dict(os.environ, SPORE_LANE=str(lane), SPORE_WARM='1' if warm else '0', PYTHONUNBUFFERED='1',
               # the test's key frames go into the run folder (twoinst.keyframe, docs/06-testing.md#testing-key-frames)
               SPORE_LAB_RUN=os.path.abspath(logdir), SPORE_LAB_TEST=t['name'] + suffix)
    # a quick run's shortened test knows it (docs/06-testing.md#testing-lab-speed); every other test runs full length
    env['SPORE_LAB_QUICK'] = '1' if t.get('quick_run') else '0'
    if t.get('monitor') is not None:  # the invariant monitor's settings (tools/tests/monitor.py)
        env['SPORE_MONITOR'] = t['monitor'] if isinstance(t['monitor'], str) else json.dumps(t['monitor'])
    log = os.path.join(logdir, t['name'] + suffix + '.log')
    t0 = time.time()
    reports0 = report_files(lane)
    marks0 = {who: loggates.log_marks(p) for who, p in game_logs(lane).items()}  # where the test's lines begin
    xw = XwSampler()
    xw.start()
    with open(log, 'w') as f:
        f.write(f'# {t["cmd"]}  (lane {lane}, {"warm" if warm else "cold"})\n')
        f.flush()
        mem = {}
        rc = run_cmd(t['cmd'], tree, env, f, t.get('timeout') or TIMEOUT, lane, mem)
    xwl = xw.result()
    # a crash after the checks is still a crash (CLAUDE.md 3, BUG-0036): a game that answered at the
    # end must still answer a few seconds later, and no new crash / hang report may appear
    alive0 = lane_alive(lane)
    time.sleep(AFTER if any(alive0.values()) else 1.0)
    alive1 = lane_alive(lane) if any(alive0.values()) else alive0
    died = [i for i, up in alive0.items() if up and not alive1.get(i)]
    new_reports = sorted(p for p in report_files(lane) - reports0 if test_game_report(lane, p))
    text = open(log, errors='replace').read().split('\n', 1)[-1]  # the test's output, not the '# cmd' line
    watched = ''
    # BUG-0249: a test that plays on the free lane outside the run is watched there from the moment
    # it held the lane's lock (before that, another run may own the lane: no snapshot)
    w = free_lane_watch(text) if t.get('uses_free_lane') else None
    if w and w[0] != lane:
        fl, since = w
        watched = f' (+{fl})'
        f0 = {i: up for i, up in lane_alive(fl).items() if i == 2 * fl - 1}
        if any(f0.values()):
            time.sleep(AFTER)
            f1 = lane_alive(fl)
            died += [i for i, up in f0.items() if up and not f1.get(i)]
        for p in sorted(report_files(fl)):
            try:
                # 2 s: file times come from the kernel's coarse clock, a bit behind time.time()
                if os.path.getmtime(p) >= since - 2 and test_game_report(fl, p):
                    new_reports.append(p)
            except OSError:
                pass
    save_game_logs(lane, logdir, t['name'] + suffix, t0)
    for p in new_reports:  # the next start of the lane may prune them
        try:
            shutil.copy(p, os.path.join(logdir, f'{t["name"]}{suffix}.{os.path.basename(p)}'))
        except OSError:
            pass
    has_result = 'RESULT:' in text or t['cmd'].startswith('make')
    skipped = 'RESULT: SKIP' in text and 'RESULT: FAIL' not in text
    faults = [l.strip() for l in text.splitlines() if ' faults: ' in l and 'total faults: 0' not in l]
    lab_fails, lab_notes = [], []
    if not t.get('reports'):
        lab_fails += [f'FAIL lab: instance {i} stopped answering after the test (crash after the checks?)'
                      for i in died]
        lab_fails += [f'FAIL lab: new report {os.path.basename(p)}' for p in new_reports]
        lab_fails += [f'FAIL lab: faults in the log: {l}' for l in faults if fault_count(l) > 0]
        # BUG-0623: a hook the mod could not make (two hooks of one function: the second fails and its fix is off)
        for gl in sorted(glob.glob(os.path.join(logdir, f'{t["name"]}{suffix}.*game.log'))):
            try:
                with open(gl, errors='replace') as f:
                    bad = sorted({m.group(1) for m in re.finditer(r'hook_create ([0-9A-Fa-f]+) failed: (MH_ERROR_\w+)', f.read())})
            except OSError:
                bad = []
            lab_fails += [f'FAIL lab: hook not made in {os.path.basename(gl)}: {a}' for a in bad]
    # BUG-0975 (feat/lab-speed): a game that dies in the warm reset of the next test (the state the test before left
    # killed it) made twoinst start cold and the test passed: no one saw the crash. It fails the test now, with the
    # lane's logs the restart moved to logs/previous
    # (a console that timed out is only said: a slow game, not proven gone; 19 such lines in the recorded runs, against
    # 27 lost games before, 25 of them in Cell tests)
    m = re.search(r'\(warm reset failed: (?!cold start)(.*?) ?\)$', text, re.M)
    lost = m and ('Errno' in m.group(1) or 'closed' in m.group(1))
    if m and not lost:
        lab_notes.append(f'warm reset: {m.group(1)[:80]} (a game too slow to answer: cold start)')
    if lost and not t.get('reports'):
        lab_fails.append(f'FAIL lab: the warm reset lost a game ({m.group(1)[:80]}): a crash in the state the test '
                         f'before left? (logs/previous copied as {t["name"]}{suffix}.previous.*)')
        prev = os.path.join(lane_game(lane), 'SporebinEP1', 'mods', 'logs', 'previous')
        for p in glob.glob(os.path.join(prev, 'sporekit*.log')):
            try:
                if os.path.getmtime(p) >= t0 - 2:
                    shutil.copy(p, os.path.join(logdir, f'{t["name"]}{suffix}.previous.{os.path.basename(p)}'))
            except OSError:
                pass
    gate_fails, gate_notes, _ = log_gates(t, lane, text, marks0, t0)
    gate_notes = lab_notes + gate_notes
    lab_fails += gate_fails
    if mem.get('over'):  # BUG-0916: also for a test that makes reports on purpose
        lab_fails.insert(0, 'FAIL lab: ' + mem['over'])
    ok = rc == 0 and 'RESULT: FAIL' not in text and 'Traceback' not in text and has_result and not lab_fails
    # the machine, not the mod: the game did not even start
    env = not ok and ('did not start' in text or "bwrap: Can't find" in text)
    m = re.search(r'\((both games ready|warm reset) in (\d+) s', text)
    load = bool(not ok and xwl and xwl['peak'] >= XW_LOAD)  # BUG-0663
    if load:
        lab_fails.append(f'LOAD lab: the headless Xwayland used {xwl["peak"]:.0%} of a core in the busiest {XW_STEP:.0f} s '
                         f'({xwl["avg"]:.0%} on average): saturated, the fps of this run are the machine\'s (BUG-0663)')
    return dict(name=t['name'], lane=lane, watched=watched, ok=ok, rc=rc, env=env, skipped=skipped and ok, xwayland=xwl, load=load,
                quick=bool(t.get('quick_run')),
                mem={k: v for k, v in mem.items() if k != 'over'},
                harness=(not has_result or 'Traceback' in text) and not lab_fails,
                seconds=round(time.time() - t0), start=f'{m.group(1)} {m.group(2)} s' if m else '',
                fails=(lab_fails + [l.strip() for l in text.splitlines() if l.strip().startswith('FAIL')])[:8],
                faults=faults, reports=[os.path.basename(p) for p in new_reports], died=died, log=log,
                gates=gate_fails + gate_notes, log_marks={w: m for w, m in marks0.items() if m})


def measured_seconds(dirs=None, keep=5):
    """{(test, quick): median seconds of its last `keep` passed runs} over the recorded runs (out/lab of the main
    checkout and of the worktrees): the scheduler's estimate (docs/06-testing.md#testing-lab-speed). The suite's
    `minutes` were rough (play_space 14 said, 13 taken; long_creature 60 said, 46 taken; unit 1 said, 6 taken)"""
    try:
        dirs = dirs if dirs is not None else loggates.lab_dirs(
            os.path.dirname(git('rev-parse', '--path-format=absolute', '--git-common-dir')))
        seen = {}
        for rundir, _, r in loggates.recorded(dirs):
            if r.get('ok') and not r.get('skipped') and r.get('seconds'):
                seen.setdefault((r['name'].split('#')[0], bool(r.get('quick'))), []).append(
                    (os.path.basename(rundir), r['seconds']))
    except Exception:  # noqa: BLE001 (an estimate: without it the suite's minutes)
        return {}
    out = {}
    for k, v in seen.items():
        last = sorted(x[1] for x in sorted(v)[-keep:])
        out[k] = last[len(last) // 2]
    return out


def expected_seconds(t, took):
    """how long a test of this run will take: its recorded median, else the suite's minutes"""
    return took.get((t['name'].split('#')[0], bool(t.get('quick_run')))) or t['minutes'] * 60


def new_run_dir(branch=None, now=None):
    """BUG-0353 / BUG-0362: a run directory of this run only. The id is the start time to the second (and the
    branch); a run started in the same second as another one (two lab processes at once) gets the next free
    suffix -2, -3, ... The directory is taken with os.mkdir, which fails when it exists, so two processes that
    race for one name never both get it. COOP_FIX-like switch for the self-test: SPORE_LAB_SHARED_RUNDIR=1
    keeps the old behaviour (one shared directory)."""
    base = os.path.join(ROOT, 'out', 'lab')
    os.makedirs(base, exist_ok=True)
    stamp = (now or datetime.datetime.now()).strftime('%Y%m%d-%H%M%S')
    tail = '-' + branch.replace('/', '-') if branch else ''
    if os.environ.get('SPORE_LAB_SHARED_RUNDIR') == '1':  # the old behaviour, for the self-test's FAIL case
        os.makedirs(os.path.join(base, stamp + tail), exist_ok=True)
        return stamp + tail, os.path.join(base, stamp + tail)
    for k in range(1, 1000):
        run_id = stamp + ('' if k == 1 else f'-{k}') + tail
        try:
            os.mkdir(os.path.join(base, run_id))
        except FileExistsError:
            continue
        return run_id, os.path.join(base, run_id)
    raise RuntimeError(f'lab: no free run directory for {stamp}{tail}')


def cmd_run(a):
    tree = worktree(a.branch) if a.branch else ROOT
    tests = suite.select(a.tests)
    quick = getattr(a, 'quick', False) or os.environ.get('SPORE_LAB_QUICK') == '1'
    if quick:  # docs/06-testing.md#testing-lab-speed: shorter play scenarios, marked in the results (labcov skips them)
        tests = [dict(t, cmd=suite.quick_cmd(t), quick_run=True, minutes=max(2, t['minutes'] // 2)) if t.get('quick') else t
                 for t in tests]
    if a.repeat > 1:  # hunting a flaky bug: the same tests many times
        tests = [dict(t, name=f'{t["name"]}#{k + 1}') for k in range(a.repeat) for t in tests]
    run_id, logdir = new_run_dir(a.branch)
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
    if getattr(a, 'lane', None):  # BUG-0259: an explicit lane list
        bad = [n for n in a.lane if not 1 <= n <= 10 or n in skip]
        if bad:
            sys.exit(f'lab: --lane {bad} is not a lane 1..10 outside --skip')
        a.lane_ids = sorted(set(a.lane))[:cap]
        a.lanes = len(a.lane_ids)
    # a lane for tests that take a whole lane of their own outside the run (update_check.py wipes
    # the mod from its lane): neither used by this run nor by other work (--skip); '' = none
    # BUG-0468: never lane 8 (the owner's) or 1, never a lane whose games answer (other work: another run or
    # agent), and lanes 9-10 count too; one such test at a time (free_lock below)
    def _free(n):
        if n in (1, 8) or n in a.lane_ids or n in skip:
            return False
        try:
            return not any(lane_alive(n).values())
        except Exception:  # noqa: BLE001
            return False
    os.environ['SPORE_FREE_LANE'] = str(next((n for n in range(2, 11) if _free(n)), ''))
    print(f'lab run {run_id}: {len(tests)} tests on {a.lanes} lane(s), tree {tree} [{tree_branch}] ({head})'
          + (f'; QUICK: {sum(bool(t.get("quick_run")) for t in tests)} shortened' if quick else ''))
    if not a.no_build:
        build(tree)
    ensure_lanes(a.lane_ids)
    for lane in a.lane_ids:  # a run starts from clean games
        spore(tree, lane, 'kill')
    took = measured_seconds()  # feat/lab-speed: longest first by what the tests took lately, not the rough minutes
    parallel = sorted([t for t in tests if not t['exclusive']], key=lambda t: -expected_seconds(t, took))
    alone = [t for t in tests if t['exclusive']]
    pending = list(parallel)
    results, lock = [], threading.Lock()
    stopping = _stopping
    stopping.clear()
    free_used = []  # free lanes a uses_free_lane test of this run may have taken

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
            shown = r['fails'] + [x for x in r['faults'] if not any(x in y for y in r['fails'])] + \
                [x for x in r.get('gates', []) if not x.startswith('FAIL')]
            print(f'  {flag} {r["name"]:<15} lane {r["lane"]}{r.get("watched", "")}  {r["seconds"]:>4} s  {r["start"]}{extra}'
                  + ''.join(f'\n         {x}' for x in shown), flush=True)

    free_lock = threading.Lock()  # BUG-0468: two tests on the one free lane at once broke both
    gate = MemGate(lambda x: print(x, flush=True))  # BUG-0916

    def gated(t, lane, warm_ok):
        """attempt() behind the memory gate: (result, warm_ok), or None after an interrupt"""
        tok = gate.admit(t, stopping)
        if tok is None:
            return None
        if isinstance(tok, str):
            return dict(name=t['name'], lane=lane, ok=False, rc=-1, seconds=0, start='', faults=[], fails=[tok]), warm_ok
        try:
            return attempt(t, lane, warm_ok)
        finally:
            gate.release(tok)

    def attempt(t, lane, warm_ok):
        if t.get('uses_free_lane'):
            with free_lock:
                return attempt1(t, lane, warm_ok)
        return attempt1(t, lane, warm_ok)

    def attempt1(t, lane, warm_ok):
        if t.get('uses_free_lane') and os.environ.get('SPORE_FREE_LANE'):
            free_used.append(int(os.environ['SPORE_FREE_LANE']))
        r = run_one(t, lane, tree, logdir, t['warm'] and warm_ok and not a.cold)
        if stopping.is_set():
            return r, False
        if not r['ok'] or not t['warm']:
            spore(tree, lane, 'kill')  # a clean slate after a failure or a test with its own setup
            warm_ok = False
        else:
            warm_ok = True
        for k in range(2):  # environment trouble (the game did not start): again, not counted as flaky
            if r['ok'] or not r.get('env') or stopping.is_set():
                break
            spore(tree, lane, 'kill')
            time.sleep(5)
            if stopping.is_set():  # BUG-0250: an interrupt during the pause starts nothing new
                break
            r = run_one(t, lane, tree, logdir, False, f'.env{k + 1}')
            r['env_retries'] = k + 1
        # harness trouble only (no RESULT line, a traceback, a timeout): once more on a clean lane, passing
        # = flaky. A RESULT: FAIL, a crash or a fault is a failure of the run (BUG-0073)
        if not r['ok'] and a.retry and r['harness'] and not r.get('env') and not stopping.is_set():
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
            got = gated(t, lane, warm_ok)
            if got is None:
                return
            r, warm_ok = got
            last = t['fixture']
            report(r)

    def shutdown():
        """BUG-0250: on Ctrl+C / SIGTERM in any phase: no new test, every test group of ours killed, the
        games of the run's lanes ended, the free lane only when nobody holds its lock. Idempotent."""
        if getattr(shutdown, 'done', False):
            return
        shutdown.done = True
        old_int = signal.signal(signal.SIGINT, signal.SIG_IGN)  # a second Ctrl+C must not cut this short
        try:
            stopping.set()
            with _procs_lock:
                for p in list(_procs):
                    kill_group(p)
            print('lab: interrupted: tests stopped, ending the games of lanes ' + ', '.join(map(str, a.lane_ids)),
                  flush=True)
            for lane in a.lane_ids:
                spore(tree, lane, 'kill')
            for fl in sorted(set(free_used) - set(a.lane_ids)):
                import fcntl
                try:
                    lk = open(free_lane_lock(fl), 'a')
                except OSError:
                    continue
                try:
                    fcntl.flock(lk, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except OSError:
                    print(f'lab: free lane {fl} is in use by another run: left alone', flush=True)
                    lk.close()
                    continue
                try:
                    spore(tree, fl, 'kill')
                finally:
                    fcntl.flock(lk, fcntl.LOCK_UN)
                    lk.close()
        finally:
            signal.signal(signal.SIGINT, old_int)

    def on_term(signum, frame):  # SIGTERM (a supervising agent stopping the lab): as Ctrl+C
        raise KeyboardInterrupt
    old_term = signal.signal(signal.SIGTERM, on_term) if threading.current_thread() is threading.main_thread() else None
    t0 = time.time()
    threads = [threading.Thread(target=worker, args=(lane,)) for lane in a.lane_ids]
    interrupted = False
    try:
        for th in threads:
            th.start()
            time.sleep(5)  # Bottles sandboxes starting at the same moment can race
        for th in threads:
            th.join()
        if alone:  # the machine alone: no game of another lane may be visible (LAN discovery)
            for lane in a.lane_ids:
                spore(tree, lane, 'kill')
        # exclusive tests run on a lane of this run (never on one --skip reserved for other work); LAN
        # discovery (the UI test) only sees hosts of lanes 1..5
        ex_lane = next((n for n in a.lane_ids if n <= 5), None)
        for t in alone:
            if stopping.is_set():
                break
            if ex_lane is None:
                report(dict(name=t['name'], lane=0, ok=True, skipped=True, rc=0, seconds=0, start='', faults=[],
                            fails=[f'exclusive: needs one of lanes 1-5 in this run (lanes {a.lane_ids})']))
                continue
            got = gated(t, ex_lane, False)
            if got is None:
                break
            report(got[0])
    except KeyboardInterrupt:  # the tests run in their own sessions: Ctrl+C does not reach them
        interrupted = True
        shutdown()
        for th in threads:
            if th.is_alive():
                th.join(timeout=60)
        raise
    finally:
        if old_term is not None:
            signal.signal(signal.SIGTERM, old_term)
        if not interrupted:
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
    import fcntl
    hist_lock = open(hist_path + '.lock', 'a')  # BUG-0362: two runs ending together each read-modify-write it
    fcntl.flock(hist_lock, fcntl.LOCK_EX)
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
    tmp_hist = f'{hist_path}.{os.getpid()}'
    with open(tmp_hist, 'w') as f:
        json.dump(hist, f, indent=1)
    os.replace(tmp_hist, hist_path)
    fcntl.flock(hist_lock, fcntl.LOCK_UN)
    hist_lock.close()
    json.dump(dict(run=run_id, tree=tree, branch=tree_branch, head=head, skipped=skipped, lanes=a.lanes, seconds=round(total), ok=ok,
                   quick=quick, results=results),
              open(os.path.join(logdir, 'results.json'), 'w'), indent=1)
    lines = [f'# lab run {run_id}', '', f'{tree_branch} {head} - {len(results)} tests, {a.lanes} lanes, '
             f'{total / 60:.1f} min, ' + (f'**all passed**{notes}' if ok else f'**FAILED**{notes}'), '']
    if quick:
        lines += ['**QUICK run** (`--quick`): tests marked (quick) ran shortened; they do not count for the release rule '
                  '(docs/06-testing.md#testing-lab-speed).', '']
    lines += ['| test | result | lane | s | start |', '|---|---|---|---|---|']
    for r in sorted(results, key=lambda r: (r['ok'], r['name'])):
        res = 'skip' if r.get('skipped') else 'ok' if r['ok'] else 'FAIL'
        lines.append(f'| {r["name"]} | {res}{" (flaky)" if r.get("flaky") else ""}{" (load)" if r.get("load") else ""}'
                     f'{" (quick)" if r.get("quick") else ""} | {r["lane"]} '
                     f'| {r["seconds"]} | {r["start"]} |')
    mems = [r for r in results if (r.get('mem') or {}).get('script_rss_mb') or (r.get('mem') or {}).get('games_rss_mb')]
    if mems:  # docs/06-testing.md#testing-lab-memory
        lows = [r['mem']['avail_min_gb'] for r in mems if r['mem'].get('avail_min_gb') is not None]
        lines += ['', '## Memory', '', 'Peak RSS of each test\'s script and of its lane\'s games'
                  + (f'; lowest MemAvailable during the run {min(lows):.1f} GB' if lows else '') + '.', '',
                  '| test | script MB | games | games MB |', '|---|---|---|---|']
        lines += [f'| {r["name"]} | {r["mem"].get("script_rss_mb", 0)} | {r["mem"].get("games", 0)} | {r["mem"].get("games_rss_mb", 0)} |'
                  for r in sorted(mems, key=lambda r: -(r['mem'].get('script_rss_mb') or 0))]
    gated = [r for r in results if r.get('gates')]
    if gated:  # docs/06-testing.md#testing-log-gates: what the games' logs said, failed or allowed
        lines += ['', '## Log gates', ''] + [f'- {r["name"]}: {g}' for r in sorted(gated, key=lambda r: r['name'])
                                              for g in r['gates']]
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
    # BUG-0259: no prefix matching: `run --lane 2` was taken as `--lanes 2` and ran on lane 1
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                 allow_abbrev=False)
    sub = ap.add_subparsers(dest='cmd', required=True)
    _add = sub.add_parser
    sub.add_parser = lambda *x, **k: _add(*x, allow_abbrev=False, **k)
    r = sub.add_parser('run')
    r.add_argument('tests', nargs='?', default='smoke', help='smoke | full | nightly | name,name')
    r.add_argument('--lanes', type=int, default=int(os.environ.get('SPORE_LANES', '3')))
    r.add_argument('--skip', default='', help='lanes other work uses, e.g. 3,6: the run takes the next free ones')
    r.add_argument('--lane', type=int, action='append', help='exactly this lane (repeatable); overrides --lanes')
    r.add_argument('--branch')
    r.add_argument('--cold', action='store_true', help='no warm reuse of running games')
    r.add_argument('--no-build', action='store_true')
    r.add_argument('--retry', type=int, default=1)
    r.add_argument('--repeat', type=int, default=1, help='run every test N times (flaky bug hunt)')
    r.add_argument('--quick', action='store_true', help='the long play scenarios shortened (suite.py quick=; also '
                   'SPORE_LAB_QUICK=1): intermediate runs only, the release rule does not count them')
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
    cv = sub.add_parser('coverage')
    cv.add_argument('--days', type=float, default=7.0)
    cv.add_argument('--commit')
    cv.add_argument('--require', default='smoke,full')
    sub.add_parser('fixcheck')
    gt = sub.add_parser('gates')
    gt.add_argument('dirs', nargs='*')
    a = ap.parse_args()
    if a.cmd in ('coverage', 'fixcheck'):
        import labcov
        return labcov.cmd_coverage(a) if a.cmd == 'coverage' else labcov.cmd_fixcheck(a)
    if a.cmd == 'gates':
        return loggates.main(['report', *a.dirs])
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
