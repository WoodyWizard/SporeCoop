"""Shared setup for two-instance tests: host = instance 1 (profile test) with a fixture save,
guest = instance 2 (empty profile) that joins from the menu and receives the host world."""
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console, recompress_png  # noqa: E402
import lane  # noqa: E402
import fixtures  # noqa: E402
from lane import APPDATA, sh  # noqa: E402,F401


def wait_up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            if Console(inst).call('ping')['ok']:
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def host_frame(con):
    import re
    m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    return int(m.group(1)) if m else -1


def wait_guest_in(host, guest, timeout=240, close=lambda h: h.call('key enter')):
    """The host saves before sharing its world and the game shows a modal "game saved" box (the
    host's frames stop): close it whenever it is up, until the guest is in the world."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        f1 = host_frame(host)
        time.sleep(0.7)
        if host_frame(host) == f1:
            close(host)
        # in the host's world: the guest plays and the host sees it in the same place. The guest's own id
        # (not always 1: a rejoin that reaches the host before its old entry is gone gets id 2,
        # rejoin_race_long round 12, lab 20261101-134020)
        if guest.call('coop.avatar')['ok']:
            import re
            m = re.search(r'client id=(\d+) connected', guest.call('coop.status').get('out', ''))
            if f'together {m.group(1) if m else 1} 1' in host.call('coop.status')['out']:
                return True
    return False


def host_up(host, tries=3):
    """BUG-0302: `coop host` on the host; a host that failed (its UDP port taken: the lab's fixed
    ports lie inside Linux's ephemeral range 32768-60999) is said and tried again, instead of a guest
    whose join times out and a test that fails 4 minutes later on something else"""
    out = ''
    for i in range(tries):
        r = host.call(lane.HOST_CMD)
        out = (r.get('out') or '').strip()
        if 'hosting on' in out or 'host failed' not in out:
            return True
        print(f'  (the host did not host, try {i + 1}: {out.splitlines()[0][:160]})', flush=True)
        time.sleep(3)
    return False


def lab_mark(what, state):
    """a line for the lab (tools/loggates.py): `LAB-MARK sync-force on|off <time>` around a sync-check
    difference the test makes on purpose (`coop.diag sync force`), so the sync-check gate leaves the
    MISMATCH lines of that window out (docs/06-testing.md#testing-log-gates)"""
    print(f'LAB-MARK {what} {state} {time.time():.3f}', flush=True)


def shot(con, name):
    path = os.path.join(lane.out_dir('shots'), name)
    if con.call('shot ' + path)['ok']:
        recompress_png(path, 0.5)
    return path


class Monitor(threading.Thread):
    """Invariants during a test (docs/10-dev-pipeline.md): a game whose frames stop for more than
    FREEZE seconds is frozen (the SporeKit watchdog logs the main thread stack at 30 s); a game whose
    console stops answering has crashed. Uses only any-thread console commands (frame, stack),
    so it works while the main thread hangs. Checks.faults() fails the test on any event."""
    FREEZE = 10.0
    LOAD_FREEZE = 60.0  # a world load blocks the main thread: civ planets took 14 s with 16 games running
    events = []

    def game_log(self):
        """the game log of this instance (lane.game_log: right from a worktree too, BUG-0068)"""
        return lane.game_log(self.inst)

    def loading(self):
        """the game's last log lines say it is loading a world"""
        try:
            with open(self.game_log(), 'rb') as f:
                f.seek(0, 2)
                f.seek(max(0, f.tell() - 1500))
                tail = [l for l in f.read().decode('utf-8', 'replace').splitlines() if '[DBG]' not in l]
        except OSError:
            return False
        return bool(tail) and ('world: loading ' in tail[-1] or 'loading the world of' in tail[-1])

    def answers_again(self, secs):
        """seconds until a fresh console connection answers `frame`, None when it does not"""
        t0 = time.time()
        while time.time() - t0 < secs:
            try:
                if Console(self.inst, timeout=5).call('frame').get('ok'):
                    return time.time() - t0
            except (OSError, ValueError):
                pass
            time.sleep(0.5)
        return None

    def __init__(self, inst, name):
        super().__init__(daemon=True)
        self.inst, self.name, self.stop = inst, name, False

    def run(self):
        import re
        con = Console(self.inst, timeout=10)
        last, since, reported = -1, time.time(), False
        while not self.stop:
            try:
                m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
            except (OSError, ValueError) as e:
                if self.stop:
                    return
                # BUG-0261: SporeKit's console serves every client on one thread and runs the waiting
                # commands (wait.frames, shot: SK_CMD_NOT_ON_MAIN) right there, so the test's own
                # `wait.frames 300 60000` held this `frame` for 10+ s on a busy lane (the game drew
                # on). A console that answers a fresh connection again is no crash; the frame check
                # below still catches a freeze. SPORE_MONITOR_STRICT=1: the old verdict (tests).
                t0 = time.time()
                back = self.answers_again(30)
                if back is not None and not os.environ.get('SPORE_MONITOR_STRICT'):
                    print(f'  ({self.name}: the console did not answer for 10 s ({e!r}) and answered a new '
                          f'connection {back:.1f} s later: busy, not a crash)', flush=True)
                    con = Console(self.inst, timeout=10)
                    continue
                Monitor.events.append(f'{self.name}: the console stopped answering (crash?) [{e!r}, '
                                      f'loading={self.loading()}, '
                                      + (f'answered again after {back:.1f} s' if back is not None else
                                         f'no answer for {time.time() - t0:.0f} s more') + ']')
                return
            f = int(m.group(1)) if m else -1
            now = time.time()
            if f != last:
                if reported:
                    Monitor.events.append(f'{self.name}: frames resumed after {now - since:.0f} s')
                elif now - since > self.FREEZE:
                    print(f'  ({self.name}: no frames for {now - since:.0f} s while loading a world)', flush=True)
                last, since, reported = f, now, False
            elif not reported and now - since > (self.LOAD_FREEZE if self.loading() else self.FREEZE):
                reported = True
                # no stack from here: suspending the main thread under Wine can hang the console too;
                # the SporeKit watchdog writes it into the game log after 30 s (lab.py keeps that log)
                Monitor.events.append(f'{self.name}: FROZE (no frame for {self.FREEZE:.0f} s; main thread stack: '
                                      f'watchdog "no frames" in the {self.name} game log)')
            time.sleep(1)


_monitors = []
_watch = None  # the invariant monitor of the test's games (monitor.py, docs/06-testing.md#testing-monitor)


def monitor_cfg():
    """SPORE_MONITOR: off, or a JSON dict of monitor.DEFAULTS overrides (the lab sets it from suite.py
    monitor=); SPORE_NET set: the link invariant is off (a bad network on purpose)"""
    import json
    v = os.environ.get('SPORE_MONITOR', '').strip()
    if v == 'off':
        return None
    try:
        cfg = json.loads(v) if v else {}
    except ValueError:
        print(f'  (SPORE_MONITOR is not JSON: {v!r}; defaults)', flush=True)
        cfg = {}
    return dict(cfg, netem=bool(os.environ.get('SPORE_NET')))


def start_watch(players):
    """the invariant monitor over {who: instance} (again: a new one, the old one stopped)"""
    global _watch
    import monitor
    if _watch is not None:
        _watch.stop_ev.set()
        _watch = None
    cfg = monitor_cfg()
    if cfg is None:
        return None
    _watch = monitor.Watch({who: (inst, lane.game_log(inst)) for who, inst in players.items()}, cfg,
                           console=lambda inst: Console(inst, timeout=10),
                           out=lambda line: print(line, flush=True))
    _watch.start()
    return _watch


def monitor_add(who, inst):
    """another game of the test (the extra guests of a party, coop_multi.start_extra)"""
    if _watch is not None:
        _watch.add_player(who, inst, lane.game_log(inst))


def monitor_flush():
    """the monitor reads the logs once more and prints what it found (before the RESULT line)"""
    if _watch is not None:
        try:
            _watch.step()
        except Exception as e:  # noqa: BLE001
            print(f'  (monitor: {e!r})', flush=True)
        if _watch.violations:
            print(f'  monitor: {len(_watch.violations)} invariant violation(s) (MONITOR-VIOLATION lines; the lab '
                  f'fails the test on them)', flush=True)


def watch(host_inst, guest_inst):
    for m in _monitors:
        m.stop = True
    _monitors.clear()
    Monitor.events.clear()
    for inst, name in ((host_inst, 'host'), (guest_inst, 'guest')):
        m = Monitor(inst, name)
        m.start()
        _monitors.append(m)
    start_watch({'host': host_inst, 'guest': guest_inst})


def build_id():
    """what is deployed: the build outputs and the symbol files (size + mtime)"""
    import glob
    import hashlib
    h = hashlib.sha1()
    for f in sorted(glob.glob(os.path.join(ROOT, 'build', '*.dll')) + glob.glob(os.path.join(ROOT, 'build', '*.package'))
                    + glob.glob(os.path.join(ROOT, 'data', 'symbols', '*.sym'))):
        st = os.stat(f)
        h.update(f'{os.path.basename(f)} {st.st_size} {st.st_mtime_ns}'.encode())
    return h.hexdigest()[:16]


STATE = os.path.join(lane.out_dir(), 'lane_state.json')


def lane_state(**kw):
    import json
    if kw:
        json.dump(kw, open(STATE, 'w'))
        return kw
    try:
        return json.load(open(STATE))
    except (OSError, ValueError):
        return {}


def alive(inst):
    try:
        return Console(inst, timeout=5).call('ping')['ok']
    except OSError:
        return False


def restore_creations(guest=True):
    """both players' own creations ("My Creations", EditorSaves.package) are test data too: every
    lane starts with the same ones (fixtures creations-host / creations-guest). guest=False: the
    guest starts with none (a player who never used the Creature Creator)"""
    import shutil
    fx = os.path.join(os.path.dirname(os.path.dirname(subprocess.run(
        ['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT, capture_output=True,
        text=True).stdout.strip())), 'SporeCoop', 'out', 'fixtures')
    if not guest:
        subprocess.run(['rm', '-f', os.path.join(lane.GUEST_DIR, 'EditorSaves.package')])
    for name, d in (('creations-host', lane.HOST_DIR), ('creations-guest', lane.GUEST_DIR)):
        if name == 'creations-guest' and not guest:
            continue
        src = os.path.join(fx, name + '.package')
        if os.path.exists(src):
            os.makedirs(d, exist_ok=True)
            shutil.copy(src, os.path.join(d, 'EditorSaves.package'))


# {'host'|'guest': {'creature'|'cell': '<hex instance>'}}: choices.ini written for a cold start, as
# if the player chose them in an earlier session (tests of BUG-0342 / BUG-0344)
PRESET_CHOICES = {}
FIXTURE_BAD = []  # what the loaded fixture lacks (start()): Checks.faults() fails the test on it (BUG-0124)


def check_fixture(host, fixture):
    bad = [what for what, ok in fixtures.check(host, fixture) if not ok]
    FIXTURE_BAD[:] = [f'fixture {fixture} lacks: {", ".join(bad)} (tools/tests/fixtures.py)'] if bad else []
    if bad:
        print(f'  (fixture {fixture} lacks: {", ".join(bad)} - tools/tests/fixtures.py)', flush=True)
    return not bad


def warm_reset(host, guest, fixture, planet):
    """the running games of the lane start the test again: the fixture goes into a fresh save
    slot (the loaded one must not change on disk), the host loads it, the guest joins again"""
    t0 = time.time()
    st = lane_state()
    slot = 10 + (st.get('slot', 9) - 9) % 10
    for c in (guest, host):
        c.call('coop.leave')
    sh('fixture', 'slot', fixture, lane.HOST_PROFILE, str(slot))
    host.call(f'coop.load Game{slot} {planet.split("/")[1]}')
    phases = []
    while time.time() - t0 < 120:
        m = host.call('game.mode')['out']
        if 'Load' not in m and 'Menu' not in m and host.call('coop.avatar')['ok']:
            break
        time.sleep(0.3)
    else:
        return False
    phases.append(('host in game', time.time() - t0))
    host.call(lane.HOST_CMD)
    guest.call(lane.JOIN)
    if not wait_guest_in(host, guest, 120):
        return False
    # the guest rejoins in its old copy of the same stage, so it is "together" at once; the host's
    # world arrives a few seconds later and the guest leaves to the galaxy menu to load it (a test
    # that started meanwhile played ~10 s in the old world, then lost the guest: play_space.py)
    def left():
        return any(m in guest.call('game.mode')['out'] for m in ('GalaxyGameEntry', 'Load'))

    t1 = time.time()
    while time.time() - t1 < 15:
        if 'receiving world' in guest.call('coop.status')['out'] or left():
            while time.time() - t1 < 30 and not left():
                time.sleep(0.2)
            if not wait_guest_in(host, guest, 120):
                return False
            break
        time.sleep(0.3)
    phases.append(('guest in game', time.time() - t0))
    lane_state(**{**st, 'slot': slot, 'fixture': fixture, 'planet': planet, 'warm_runs': st.get('warm_runs', 0) + 1})
    time.sleep(3)
    print(f'  (warm reset in {time.time() - t0:.0f} s: ' + ', '.join(f'{n} {t:.0f}' for n, t in phases)
          + f'; warm run {st.get("warm_runs", 0) + 1} of these games)', flush=True)
    return True


def start(*a, **kw):
    """_start(), then SPORE_FIX_OFF=<name>[,<name>...] switches those fixes off on both games
    (`coop.fix <name> off`, BUG-0116): any test can run once without a fix to show that it still
    catches the bug. Fixes that act while the guest joins are on until then.
    Strict (BUG-0214 R8): a name the game does not know fails the test at once, so a twin run
    cannot pass silently with the fix still on; an unsafe switch only gets a warning line."""
    host, guest = _start(*a, **kw)
    names = [n for n in (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',')) if n]
    if names:  # BUG-0312: these games keep the switches off; the next warm test must not reuse them
        lane_state(**{**lane_state(), 'fix_off': ','.join(names)})
    for name in names:
        for con in (host, guest):
            # a SporeKit switch (kit.fix, sporekit/fixes.cpp) is named `kit.<name>` (BUG-0388)
            out = con.call(f'kit.fix {name[4:]} 0' if name.startswith('kit.') else f'coop.fix {name} off')['out'].strip()
            print('  SPORE_FIX_OFF: ' + out, flush=True)
            if not fix_off_ok(name, out):
                print(f'RESULT: FAIL (SPORE_FIX_OFF: unknown fix {name}: {out.splitlines()[0] if out else "no answer"})',
                      flush=True)
                raise SystemExit(1)
            if ' unsafe ' in out.splitlines()[0]:
                print(f'  SPORE_FIX_OFF: warning: {name} is unsafe (its off side can corrupt memory or data)', flush=True)
    apply_fps(host, 'host')
    apply_fps(guest, 'guest')
    return host, guest


def fps_limits(spec=None):
    """SPORE_FPS="host:15,guest:30,extra:20" (or one number for every game) -> {'host': 15.0, ...}
    (docs/06-testing.md#testing-degraded); extra1 / extra2 name one extra guest, extra both"""
    spec = os.environ.get('SPORE_FPS', '') if spec is None else spec
    out = {}
    for part in filter(None, (x.strip() for x in spec.split(','))):
        who, _, v = part.rpartition(':')
        fps = float(v)
        if not 1 <= fps <= 240:
            raise ValueError(f'SPORE_FPS: {part}: 1..240 fps')
        for w in ((who,) if who else ('host', 'guest', 'extra')):
            if w not in ('host', 'guest', 'extra', 'extra1', 'extra2'):
                raise ValueError(f'SPORE_FPS: {part}: host, guest, extra, extra1 or extra2')
            out[w] = fps
    return out


def fps_of(who, limits):
    return limits.get(who, limits.get('extra') if who.startswith('extra') else None)


def apply_fps(con, who):
    """the game's frame time cap from SPORE_FPS (`frame.limit <ms>`); these games then are no warm games"""
    fps = fps_of(who, fps_limits())
    if fps is None:
        return None
    ms = max(1, round(1000 / fps))
    out = (con.call(f'frame.limit {ms}').get('out') or '').strip()
    print(f'  SPORE_FPS: {who} at {fps:g} fps (frame.limit {ms}): {out.splitlines()[0] if out else "no answer"}',
          flush=True)
    lane_state(**{**lane_state(), 'fps': os.environ.get('SPORE_FPS', '')})
    return ms


def fix_off_ok(name, answer):
    """the coop.fix answer to `<name> off` (kit.fix: `kit.<name>` -> `<name> 0`) says that switch is off now"""
    if name.startswith('kit.'):
        return answer == f'{name[4:]} 0'
    return answer.startswith(f'{name} off ')


def _start(fixture='creature', planet=None, load_args=(), join_in_intro=False, guest_creations=True):
    """kill, deploy, restore the fixture for the host, empty guest profile, start both,
    load the host game (tools/tests/load_game.py), host + join; returns (host, guest).
    With SPORE_WARM=1 (tools/lab.py) the running games of the lane are reused when they run the
    same build: warm_reset() instead of a restart (~10 s instead of ~40 s).
    join_in_intro: the guest joins while its galaxy menu plays the intro cinematic GGE_Intro (cold
    start only; the race behind the stuck guest movie, docs/re/space.md 7.9)."""
    if planet is None and not load_args:
        planet = fixtures.planet_of(fixture)  # loaded by name (no menu clicks, warm-capable)
    st = lane_state()
    # warm only within the same save: a test starts from its fixture (stage switches in game work,
    # coop_stage_switch.py, but go through the galaxy menu and take longer)
    if os.environ.get('SPORE_WARM') == '1' and planet and not join_in_intro and st.get('build') == build_id() \
            and st.get('fixture') == fixture and st.get('guest_creations', True) == guest_creations \
            and not st.get('fix_off') and not st.get('fps') and not PRESET_CHOICES \
            and alive(lane.HOST) and alive(lane.GUEST):
        host, guest = Console(lane.HOST), Console(lane.GUEST)
        try:
            if warm_reset(host, guest, fixture, planet):
                watch(lane.HOST, lane.GUEST)
                if check_fixture(host, fixture):
                    return host, guest
                for m in _monitors:  # the reused games miss a fixture fact: start them again from the files
                    m.stop = True
                print('  (warm reset: the fixture lacks its facts)', flush=True)
        except (OSError, ValueError) as e:
            print('  (warm reset failed:', e, ')', flush=True)
        print('  (warm reset failed: cold start)', flush=True)
    lane_state()  # (the state is rewritten below once the games run)
    sh('kill')
    time.sleep(2)
    sh('deploy')
    sh('fixture', 'restore', fixture, lane.HOST_PROFILE)
    restore_creations(guest_creations)
    subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
    for d in (lane.HOST_DIR, lane.GUEST_DIR):  # own-creation choices: back to the defaults
        subprocess.run(['rm', '-f', d + '/SporeCoop/choices.ini'])
    for who, lines in PRESET_CHOICES.items():  # a test's choices from an earlier session (BUG-0342/0344)
        d = os.path.join(lane.HOST_DIR if who == 'host' else lane.GUEST_DIR, 'SporeCoop')
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'choices.ini'), 'w', newline='\r\n') as f:
            f.write('[creations]\n' + ''.join(f'{k}={v}\n' for k, v in lines.items()))
    t_start = time.time()
    # join_in_intro: the guest starts once the host is in its world, so that its menu intro comes
    # after the host is ready (the guest joins at the start of it)
    if not join_in_intro:
        lane.run_guest()
        time.sleep(2)  # two sandboxes starting at the very same moment can race on the bottle
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host))[1 if join_in_intro else 0:]:
        if not wait_up(inst, 90):  # two sandboxes starting together sometimes lose one: start it again
            # BUG-0267: the lost one may still be starting (a busy machine): a second copy of the
            # same instance next to it failed to copy coop.dll (err 32) and hung. End the lane's
            # games first, then start both again.
            print(f'  (instance {inst} did not answer in 90 s: the lane starts again)', flush=True)
            sh('kill')
            time.sleep(2)
            if not join_in_intro:
                lane.run_guest()
                time.sleep(2)
            lane.run_host()
            for i in ((lane.HOST,) if join_in_intro else (lane.GUEST, lane.HOST)):
                assert wait_up(i), f'instance {i} did not start'
            break
    phases = [('wine up', time.time() - t_start)]
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    if planet:  # GameN/Planet of the fixture, loaded through the mod (works from the galaxy menu)
        for _ in range(180):
            if 'GalaxyGameEntry' in host.call('game.mode')['out']:
                break
            time.sleep(1)
        phases.append(('host menu', time.time() - t_start))
        host.call('wait.frames 240 90000')  # the galaxy menu settles (8 s)
        slot, name = planet.split('/')
        host.call(f'coop.load {slot} {name}')
        t0 = time.time()
        while time.time() - t0 < 240 and not host.call('coop.avatar')['ok']:
            time.sleep(2)
        phases.append(('host in game', time.time() - t_start))
        time.sleep(4)
    else:
        subprocess.run([sys.executable, os.path.join(HERE, 'load_game.py'), '-i', str(lane.HOST), *load_args],
                       capture_output=True, text=True, timeout=600)
    host_up(host)
    if join_in_intro:
        lane.run_guest()
        if not wait_up(lane.GUEST, 90):
            lane.run_guest()
            assert wait_up(lane.GUEST), f'instance {lane.GUEST} did not start'
        guest = Console(lane.GUEST)
    for _ in range(120):
        if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            break
        time.sleep(1)
    phases.append(('guest menu', time.time() - t_start))
    guest.call('wait.frames 200 60000')
    if join_in_intro:  # the intro starts ~16 s after the menu came up and runs ~10 s
        for _ in range(120):
            if '(menu intro)' in guest.call('coop.status')['out']:
                break
            time.sleep(0.25)
        else:
            print('  (the guest menu intro did not start)', flush=True)
    phases.append(('guest join', time.time() - t_start))
    guest.call(lane.JOIN)
    # the host saves before sharing and the game shows a modal "game saved" box (frames stop):
    # close it whenever it is up, until the guest is in the world
    if not wait_guest_in(host, guest):
        print('  (the guest is not in the host\'s world after 240 s: '
              + guest.call('coop.status')['out'].strip().splitlines()[0][:160] + ')', flush=True)
    phases.append(('guest in game', time.time() - t_start))
    time.sleep(4)
    print(f'  (both games ready in {time.time() - t_start:.0f} s: '
          + ', '.join(f'{n} {t:.0f}' for n, t in phases) + ')', flush=True)
    lane_state(build=build_id(), fixture=fixture, planet=planet, slot=9, warm_runs=0, guest_creations=guest_creations)
    watch(lane.HOST, lane.GUEST)
    check_fixture(host, fixture)
    return host, guest


class Checks:
    def __init__(self):
        self.ok = True

    def __call__(self, cond, msg):
        print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
        self.ok &= bool(cond)
        return cond

    def faults(self, **cons):
        for name, con in cons.items():
            out = con.call('exc')['out'].splitlines()
            print(f'{name} faults: {out[0]}')
            for line in out[1:4]:
                print(f'    {line.strip()}')
            self.ok &= 'total faults: 0' in out[0]
        for e in Monitor.events + FIXTURE_BAD:  # freezes / crashes seen while the test ran, a broken fixture
            print('  FAIL invariant:', e)
            self.ok = False

    def result(self):
        monitor_flush()
        print('RESULT:', 'OK' if self.ok else 'FAIL')
        return 0 if self.ok else 1
