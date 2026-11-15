"""The machine budget of the lab (plan 0.7.15 D1 and A16, BUG-1114 / BUG-1113; docs/06-testing.md#testing-lab-budget).

The lab used to cap lanes (6), not games: the 0.7.14 final check ran 22 games on the 16 GB card where about 18 fit
(BUG-1098: garbage key frames), and every lane drew through one headless Xwayland that saturates at 10-14 games
(BUG-0663). A test now starts only when the machine has room for the games it adds:

* games: every SporeApp.exe of the machine (this run's, other runs', other worktrees') plus the games of tests
  just started (not all up yet) plus the test's own new games stay <= GAME_BUDGET (18);
* VRAM: the card's used VRAM plus GAME_VRAM_GB for each game still to come stays under its size less VRAM_FREE_GB;
* its display: the lane's headless shell (A16: lane N draws on shell (N - 1) % SPORE_HEADLESS_DISPLAYS, the rule of
  tools/headless.sh) holds at most DISPLAY_GAMES games and its Xwayland used under XW_ADMIT of a core lately;
* load-sensitive tests (suite.load_sensitive: the motion family whose measures follow the observer's fps,
  docs/06-testing.md#testing-fcgates-0714): while one runs, or before one starts, the machine stays at most
  LS_GAMES games.

A test's game count is suite.games(t) (4 for a campaign, N of --players N, 2 otherwise); the games its lane already
runs count as its own (a warm test adds none, a cold one replaces them). The decision is a pure function (decide)
for the unit test (tools/tests/labstart_selftest.py); the readings come from /proc and sysfs, read without root.
SPORE_LAB_BUDGET=0: no budget (the lab before 0.7.15).
"""
import glob
import os
import re
import subprocess
import threading
import time


def _f(name, default):
    try:
        return float(os.environ.get(name, default) or 0)
    except ValueError:
        return float(default)


GAME_BUDGET = int(_f('SPORE_LAB_GAMES', 18))  # BUG-1098: ~0.66 GB a game over ~3.9 GB base on 16 GB
GAME_VRAM_GB = _f('SPORE_LAB_GAME_VRAM_GB', 0.66)
VRAM_FREE_GB = _f('SPORE_LAB_VRAM_FREE_GB', 0.5)
DISPLAY_GAMES = int(_f('SPORE_LAB_DISPLAY_GAMES', 10))  # BUG-0663: one Xwayland saturated at 10-14 games
XW_ADMIT = _f('SPORE_LAB_XW_ADMIT', 0.8)  # TST-3: a new test waits while its display's Xwayland is over 80 %
LS_GAMES = int(_f('SPORE_LAB_LS_GAMES', 12))
RAMP = 90.0  # seconds in which a started test's games count as still to come (cold start median 33 s, p90 45)
POLL = 5.0
WAIT = _f('SPORE_LAB_BUDGET_WAIT', 1800)  # with nothing of the run running: "not started" after this
XW_STEP = 10.0


def until(cond, timeout, stopping=None, step=0.25):
    """poll cond() until true (seconds waited) or `timeout` s (None); an interrupt (stopping set) ends it too.
    SPORE_START_FIXED=1 (A9's twin): the old fixed sleep of `timeout` s"""
    t0 = time.time()
    if os.environ.get('SPORE_START_FIXED') == '1':
        (stopping.wait if stopping is not None else time.sleep)(timeout)
        return None
    while True:
        try:
            if cond():
                return time.time() - t0
        except (OSError, ValueError):
            pass
        left = timeout - (time.time() - t0)
        if left <= 0 or (stopping is not None and stopping.is_set()):
            return None
        if stopping is not None:
            stopping.wait(min(step, left))
        else:
            time.sleep(min(step, left))


def enabled():
    return os.environ.get('SPORE_LAB_BUDGET', '1') != '0'


def displays():
    try:
        n = int(os.environ.get('SPORE_HEADLESS_DISPLAYS', '2'))
    except ValueError:
        n = 1
    return n if 1 <= n <= 9 else 1


def display_name(lane, n=None):
    """the headless shell lane `lane` draws on (tools/headless.sh lane_name: the same rule)"""
    if os.environ.get('SPORE_HEADLESS_NAME'):
        return os.environ['SPORE_HEADLESS_NAME']
    n = displays() if n is None else n
    k = (max(1, int(lane)) - 1) % n
    return 'sporecoop-headless' if k == 0 else f'sporecoop-headless-{k + 1}'


def _cmdline(pid):
    try:
        return open(f'/proc/{pid}/cmdline', 'rb').read().replace(b'\0', b' ').decode('utf-8', 'replace')
    except OSError:
        return ''


def _pgrep(name):
    try:
        return [int(p) for p in subprocess.run(['pgrep', '-x', name], capture_output=True, text=True).stdout.split()]
    except (OSError, ValueError):
        return []


def xwaylands():
    """{shell name: (pid, ':N')} of the lab's headless Xwaylands (the -auth file lies in the shell's runtime folder)"""
    rt = os.environ.get('XDG_RUNTIME_DIR') or f'/run/user/{os.getuid()}'
    out = {}
    for pid in _pgrep('Xwayland'):
        cmd = _cmdline(pid)
        m = re.search(r'-auth ' + re.escape(rt) + r'/(sporecoop-headless(?:-\d+)?)/', cmd)
        d = re.match(r'\S+ (:\d+)', cmd)
        if m and d:
            out[m.group(1)] = (pid, d.group(1))
    return out


def lane_of_dir(cwd):
    """the lane of a game whose working directory is cwd (SPORE-lab/laneN/..., lane 1: any other game folder)"""
    m = re.search(r'/SPORE-lab/lane(\d+)(?:/|$)', cwd or '')
    return int(m.group(1)) if m else 1


def games():
    """[(pid, lane, DISPLAY)] of every SporeApp.exe of the machine (its environment's DISPLAY: ':N')"""
    out = []
    for pid in _pgrep('SporeApp.exe'):
        try:
            cwd = os.readlink(f'/proc/{pid}/cwd')
            env = open(f'/proc/{pid}/environ', 'rb').read().split(b'\0')
        except OSError:
            continue
        disp = next((e[8:].decode('utf-8', 'replace') for e in env if e.startswith(b'DISPLAY=')), '')
        out.append((pid, lane_of_dir(cwd), disp))
    return out


def vram_gb():
    """(used, total) GB of the GPU's VRAM (amdgpu sysfs, the largest card), (None, None) when not readable"""
    best = (None, None)
    for d in glob.glob('/sys/class/drm/card*/device'):
        try:
            used = int(open(os.path.join(d, 'mem_info_vram_used')).read()) / 2 ** 30
            total = int(open(os.path.join(d, 'mem_info_vram_total')).read()) / 2 ** 30
        except (OSError, ValueError):
            continue
        if best[1] is None or total > best[1]:
            best = (used, total)
    return best


def proc_ticks(pid):
    try:
        f = open(f'/proc/{pid}/stat').read().rsplit(')', 1)[1].split()
        return int(f[11]) + int(f[12])
    except (OSError, IndexError, ValueError):
        return None


def snapshot(xw_share=None):
    """the machine now: games in all, games per shell, VRAM, the Xwaylands' recent share of a core"""
    xw = xwaylands()
    by_disp = {d: name for name, (_, d) in xw.items()}
    g = games()
    per = {}
    per_lane = {}
    for _, lane, disp in g:
        name = by_disp.get(disp, disp or '?')
        per[name] = per.get(name, 0) + 1
        per_lane[lane] = per_lane.get(lane, 0) + 1
    used, total = vram_gb()
    return dict(games=len(g), per_display=per, per_lane=per_lane, vram_used=used, vram_total=total,
                xw=dict(xw_share or {}))


def decide(need, lane, display, snap, pending, cfg, sensitive=False, ls_running=False):
    """'' when a test that runs `need` games on `lane` (shell `display`) may start now, else why it waits.
    snap: snapshot(); pending: [(lane, display, games still to come, VRAM GB still to come)] of the tests started in
    the last RAMP s; cfg: dict(games, vram_game, vram_free, display_games, xw_admit, ls_games)."""
    have = snap['per_lane'].get(lane, 0)
    add = max(0, need - have)  # its lane's running games are its own (warm: reused, cold: replaced)
    if not need:
        return ''
    p_games = sum(p[2] for p in pending)
    p_disp = sum(p[2] for p in pending if p[1] == display)
    p_vram = sum(p[3] for p in pending)
    total = snap['games'] + p_games + add
    if add and total > cfg['games']:
        return (f'{snap["games"]} games run, {p_games} starting, {add} of its own: over the budget of {cfg["games"]} '
                f'(VRAM, BUG-1098)')
    used, size = snap.get('vram_used'), snap.get('vram_total')
    if add and used is not None and size:
        want = used + p_vram + add * cfg['vram_game']
        if want > size - cfg['vram_free']:
            return (f'VRAM {used:.1f} GB used, {p_vram:.1f} GB for games starting, {add * cfg["vram_game"]:.1f} GB for '
                    f'its own: over the card\'s {size:.1f} GB less {cfg["vram_free"]:.1f} (BUG-1098)')
    on = snap['per_display'].get(display, 0)
    if add and on + p_disp + add > cfg['display_games']:
        return (f'{on} games on its display {display}, {p_disp} starting there, {add} of its own: over '
                f'{cfg["display_games"]} on one Xwayland (BUG-0663)')
    xw = snap['xw'].get(display)
    if add and xw is not None and xw >= cfg['xw_admit']:
        return f'the Xwayland of {display} used {xw:.0%} of a core lately (admit under {cfg["xw_admit"]:.0%}, BUG-0663)'
    if (sensitive or ls_running) and total > cfg['ls_games']:
        return (f'{"it measures motion against fps" if sensitive else "a load-sensitive test runs"}: the machine '
                f'would run {total} games (at most {cfg["ls_games"]} then, docs/06-testing.md#testing-lab-budget)')
    return ''


def config():
    return dict(games=GAME_BUDGET, vram_game=GAME_VRAM_GB, vram_free=VRAM_FREE_GB, display_games=DISPLAY_GAMES,
                xw_admit=XW_ADMIT, ls_games=LS_GAMES)


class XwShares(threading.Thread):
    """every lab Xwayland's share of one core over the last XW_STEP s (shares[name])"""
    def __init__(self):
        super().__init__(daemon=True)
        self.shares, self.stop_ev = {}, threading.Event()
        self.hz = os.sysconf('SC_CLK_TCK')

    def run(self):
        last = {}
        while True:
            now = time.time()
            for name, (pid, _) in xwaylands().items():
                k = proc_ticks(pid)
                if k is None:
                    continue
                if name in last and last[name][0] == pid:
                    _, t0, k0 = last[name]
                    self.shares[name] = (k - k0) / self.hz / max(now - t0, 1e-3)
                last[name] = (pid, now, k)
            if self.stop_ev.wait(XW_STEP):
                return


class Gate:
    """the run's machine budget: admit(t, lane) blocks until decide() lets the test start (a token for release()),
    returns None after an interrupt, a 'FAIL lab: not started ...' line when nothing of the run ran for WAIT s"""
    def __init__(self, out=print, games_of=None, sensitive_of=None, snap=None, clock=time.time):
        import suite  # noqa: PLC0415 (tools/tests on the path: lab.py)
        self.games_of = games_of or suite.games
        self.sensitive_of = sensitive_of or getattr(suite, 'load_sensitive', lambda t: False)
        self.out, self.clock = out, clock
        self.lock = threading.Lock()
        self.held = []  # [start, lane, display, games, sensitive]
        self.xw = None
        self.snap = snap
        self.cfg = config()

    def _snap(self):
        if self.snap:
            return self.snap()
        if self.xw is None:
            self.xw = XwShares()
            self.xw.start()
        return snapshot(self.xw.shares)

    def pending(self, now, snap):
        out = []
        for t0, lane, disp, n, _ in self.held:
            age = now - t0
            if age >= RAMP:
                continue
            left = max(0, n - snap['per_lane'].get(lane, 0))  # games of its lane not up yet
            out.append((lane, disp, left, n * self.cfg['vram_game'] * (1.0 - age / RAMP)))
        return out

    def admit(self, t, lane, stopping=None, poll=None):
        need = self.games_of(t)
        if not enabled() or not need:
            return []
        disp = display_name(lane)
        sens = bool(self.sensitive_of(t))
        t0, said, idle_since = self.clock(), False, None
        while True:
            with self.lock:
                now = self.clock()
                snap = self._snap()
                ls_running = any(h[4] for h in self.held)
                why = decide(need, lane, disp, snap, self.pending(now, snap), self.cfg, sens, ls_running)
                if not why:
                    tok = [now, lane, disp, need, sens]
                    self.held.append(tok)
                    if said:
                        self.out(f'  (budget: {t["name"]} starts after {now - t0:.0f} s: {snap["games"]} games run)')
                    return tok
                running = len(self.held)
            if not said:
                said = True
                self.out(f'  (budget: {t["name"]} on lane {lane} waits: {why})')
            idle_since = None if running else (idle_since or now)
            if WAIT and idle_since is not None and now - idle_since >= WAIT:
                return f'FAIL lab: not started, no room on the machine after {now - t0:.0f} s: {why}'
            if stopping is not None:
                if stopping.wait(POLL if poll is None else poll):
                    return None
            else:
                time.sleep(POLL if poll is None else poll)

    def release(self, tok):
        with self.lock:
            if tok in self.held:
                self.held.remove(tok)
