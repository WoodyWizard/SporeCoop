"""Shared setup for two-instance tests: host = instance 1 (profile test) with a fixture save,
guest = instance 2 (empty profile) that joins from the menu and receives the host world."""
import os
import re
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
import memprobe  # noqa: E402
import startwait  # noqa: E402
import harness  # noqa: E402

memprobe.start()  # SPORE_MEMPROBE=1|trace: the test script's own memory (BUG-0916)


def wait_up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            if Console(inst).call('ping')['ok']:
                return True
        except (OSError, ValueError):
            # BUG-1359: ValueError (a reply that is not JSON): the port's listener is not a console that answers, e.g. the
            # lane's previous game still tearing down (evolve_world of the 0.7.16 final check died on it): not up yet
            pass
        time.sleep(1 if startwait.fixed() else startwait.STEP)  # A9: a 0.25 s poll (was 1 s)
    return False


def port_free(port):
    """nothing listens on 127.0.0.1:port (a connect is refused)"""
    import socket
    try:
        socket.create_connection(('127.0.0.1', port), timeout=0.5).close()
        return False
    except OSError:
        return True


PORTS_FREE_S = 20.0  # BUG-1359


def lane_down(timeout=2.0):
    """A9 (BUG-1112): after `spore.sh kill`, until no process of the lane is left (`spore.sh procs`), at most the
    old fixed 2 s; seconds waited, None at the bound.
    BUG-1359: and then until neither console port of the lane has a listener, at most PORTS_FREE_S s more: in the 0.7.16
    final check (20261120-041304, evolve_world) the previous test's host still held 47806 when the next cold start's
    host booted 5 s after it ended; the new game could not listen ("console: cannot listen ... (err 10013)") and the
    old one's console answered the start's ping with an empty line"""
    w = startwait.until(lambda: not subprocess.run([lane.SPORE, 'procs'], capture_output=True, text=True,
                                                   timeout=10).stdout.strip(), timeout)
    ports = [47800 + i - 1 for i in (lane.HOST, lane.GUEST)]
    if startwait.fixed() or all(port_free(p) for p in ports):
        return w
    t0 = time.time()
    f = startwait.until(lambda: all(port_free(p) for p in ports), PORTS_FREE_S)
    print(f'  (the lane\'s console ports {ports} were still held after the kill: '
          + (f'free after {time.time() - t0:.1f} s more)' if f is not None else f'still held after {PORTS_FREE_S:.0f} s)'),
          flush=True)
    return None if f is None else (w or timeout) + f


def game_process_up(inst, timeout=2.0):
    """A9: after a launch, until the instance's game runs SporeKit (its console answers: the sandbox, the wineserver
    and the game are up, the next launch no longer races it on the bottle), at most the old fixed 2 s between the two
    launches. BUG-1209: the game process alone was not enough: in the 0.7.15 final check (space_planner_build, lab
    20261116-025154) the host was launched as soon as the guest's SporeApp.exe existed, both SporeKits booted in the
    same millisecond (the only one of 836 starts) and the guest's console reset the test's first call.
    SPORE_GAME_UP_PROCESS=1: the process only (the twin)."""
    if os.environ.get('SPORE_GAME_UP_PROCESS') == '1':
        return startwait.until(lambda: subprocess.run([lane.SPORE, 'pids', str(inst)], capture_output=True, text=True,
                                                      timeout=10).stdout.strip(), timeout)
    return startwait.until(lambda: Console(inst, timeout=1).call('ping')['ok'], timeout)


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


def game_runs(con, timeout=45.0):
    """wait until the game's time runs (`coop.pauses`: paused=0). start() returns when the guest is in the stage; its
    game can still stand on the stage entry's pause (reason 04BF38A7, a black screen while the entry waits for its bakes:
    0 s on most lanes, 5 s and more on others, 10-40 s in Tribe) and real input sent then is lost (BUG-0944: the combat
    stance click of creature_kills). Returns the seconds waited, None when it still stands after timeout."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        if re.search(r'paused=0\b', con.call('coop.pauses').get('out') or ''):
            return time.time() - t0
        time.sleep(0.25)
    return None


def ability_mode(con):
    """Creature: the game's stance (cCreatureGameData::GetAbilityMode): 0 social, 1 combat, None unknown"""
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call Simulator::cCreatureGameData::GetAbilityMode cdecl').get('out') or '')
    return int(m.group(1), 16) if m else None


def set_stance(con, kind, w=1440, h=1080, tries=4):
    """Creature: the social / combat stance by the player's click on its button above the ability bar, read back from
    the game (a click while the game stands or before the HUD is up does nothing, and a second click on a stance that
    is already on is not needed): waits for the game time first (game_runs). True when the game is in the stance."""
    want = 1 if kind == 'combat' else 0
    game_runs(con)
    for _ in range(tries):
        if ability_mode(con) == want:
            return True
        con.call(f'click {w // 2 + (43 if want else -30)} {h - 82}')
        time.sleep(0.6)
    return ability_mode(con) == want


PLAYERS = {}  # {'host': Console, 'guest': Console} of the last start(): keyframe()'s default games


KEYFRAME_BUSY = ('cinematic', 'paused', 'editor', 'loading')  # BUG-0924: a frame at such a moment says nothing
KEYFRAME_SETTLE = float(os.environ.get('SPORE_KEYFRAME_SETTLE', '20'))
# BUG-0984: the game's pause menu (and a cinematic's end) fades out for about a second after the game says it plays
# again: a frame shot right after the busy state ended had the half-transparent menu over the guest's game (0.7.13
# final check, keyframe_settle). After a wait the frame waits this much longer (0: the old behaviour, the twin).
KEYFRAME_FADE = float(os.environ.get('SPORE_KEYFRAME_FADE', '1.5'))


def _game_busy(con):
    """the busy reasons of a game a key frame waits out (coop.status `busy:`, the editor modes), [] when none"""
    try:
        st = con.call('coop.status').get('out', '')
        m = re.search(r'^busy: (\S+)', st, re.M)
        why = [w for w in (m.group(1).split(',') if m else []) if w in KEYFRAME_BUSY]
        if 'Editor' in con.call('game.mode').get('out', '') and 'editor' not in why:
            why.append('editor')
        return why
    except (OSError, ValueError):
        return []


def keyframe_settle(players, settle=None, calm=0):
    """BUG-0924: wait (at most `settle` s) until no game of the frame plays a cinematic or a caption, stands in a
    pause (the pause menu, a planner), sits in an editor or loads; {role: [reasons]} still busy at the end.
    calm (BUG-0980): the games must also stay so for that many seconds (a stage intro's picture, the Cell meteor and
    its letterbox, stays a few seconds after the game's cinematic and input lock end)."""
    settle = KEYFRAME_SETTLE if settle is None else settle
    t0 = time.time()
    calm_since = None
    cons = {}
    for r, c in players.items():  # own connections: a bot thread may be using the test's console now
        try:
            cons[r] = Console(c.port - 47799, timeout=15)
        except (OSError, ValueError):
            pass
    try:
        seen = False
        while True:
            busy = {r: b for r, b in ((r, _game_busy(c)) for r, c in cons.items()) if b}
            now = time.time()
            calm_since = None if busy else (calm_since or now)
            if not busy and seen and KEYFRAME_FADE > 0 and now - calm_since >= calm:  # BUG-0984: the menu's or the cinematic's fade-out
                time.sleep(KEYFRAME_FADE)
                busy = {r: b for r, b in ((r, _game_busy(c)) for r, c in cons.items()) if b}
                if not busy:
                    return busy, time.time() - t0
                calm_since = None
            elif (not busy and now - calm_since >= calm) or now - t0 >= settle + calm:
                return busy, now - t0
            if busy and time.time() - t0 >= settle + calm:
                return busy, time.time() - t0
            seen = True
            time.sleep(0.5)
    finally:
        for c in cons.values():
            c.sock and c.sock.close()


def keyframe(tag, note, players=None, kind='key', extra=None, settle=None, calm=0):
    """a key frame (docs/06-testing.md#testing-key-frames): every game of the test is shot at the same moment
    (one thread each), stored as a ~960 px JPEG in <run>/frames/<test>/<nn>_<tag>_<role>.jpg with a manifest line
    (the note says what should be visible; each game's mode and position). players: {role: Console}, default the
    host and guest of start(); kind 'key', 'phase' (a campaign's phase end) or 'sameview'. Never fails the test:
    a game that does not answer gets ok=false in the manifest. Returns the manifest entry (None on an error).
    BUG-0924: first waits (settle s, SPORE_KEYFRAME_SETTLE, default 20; 0: at once) until no game is in a cinematic,
    a pause, an editor or a load; a game still busy then is named in the manifest's extra (`busy role: why`).
    calm: then also that many seconds with no game busy (BUG-0980: a frame right after a stage intro)."""
    import frames
    try:
        players = dict(players or PLAYERS)
        if not players:
            return None
        busy, waited = keyframe_settle(players, settle, calm) if (settle is None or settle > 0 or calm) else ({}, 0.0)
        if busy or waited >= 1:
            extra = list(extra or []) + [f'busy {r}: {",".join(b)}' for r, b in sorted(busy.items())] + \
                ([f'waited {waited:.1f} s for the games to settle'] if waited >= 1 else [])
        d = frames.frames_dir(lane.out_dir())
        nn = frames.next_number(d)
        tmp = lane.out_dir('shots', 'keyframes')
        games = {}
        t_shot = time.time()

        def one(role, con):
            g = {'file': frames.frame_file(nn, tag, role), 'ok': False, 'mode': '', 'pos': None}
            games[role] = g
            try:
                c = Console(con.port - 47799, timeout=30)
                png = os.path.join(tmp, f'{os.getpid()}_{nn}_{frames.safe(role, 12)}.png')
                if c.call('shot ' + png).get('ok') and frames.to_jpeg(png, os.path.join(d, g['file'])):
                    g['ok'] = True
                try:
                    os.remove(png)
                except OSError:
                    pass
                g['mode'] = frames.parse_mode(c.call('game.mode').get('out'))
                g['pos'] = frames.parse_pos(c.call('coop.avatar').get('out'))
                c.sock and c.sock.close()
            except (OSError, ValueError) as e:
                g['error'] = repr(e)[:120]

        ths = [threading.Thread(target=one, args=(r, c), daemon=True) for r, c in players.items()]
        for t in ths:
            t.start()
        for t in ths:
            t.join(60)
        for g in games.values():  # a frame Pillow could not write (no Pillow): no file in the manifest
            if not g['ok']:
                g['file'] = ''
        stage = next((g['mode'] for r, g in sorted(games.items(), key=lambda kv: frames.role_order(kv[0]))
                      if g.get('mode')), '')
        e = frames.now_entry(frames.test_name(), nn, tag, note, kind, stage, games, extra)
        e['shot_t'] = round(t_shot, 2)  # when the shots were asked for (BUG-0984)
        frames.record(d, e)
        shot_n = sum(1 for g in games.values() if g['ok'])
        print(f'  (key frame {nn:02d} {tag}: {shot_n} of {len(games)} games shot -> {d})', flush=True)
        return e
    except Exception as ex:  # noqa: BLE001: a key frame never fails a test
        print(f'  (key frame {tag}: {ex!r})', flush=True)
        return None


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


# BUG-0679: a guest keeps its own cargo hold per host world on its disk (<profile>/Spore/SporeCoop/holds); every test
# starts without one unless it asks for the record of its own earlier session (start(..., keep_holds=True))
KEEP_HOLDS = [False]


def forget_holds():
    for d in (lane.HOST_DIR, lane.GUEST_DIR):
        subprocess.run(['rm', '-rf', d + '/SporeCoop/holds', d + '/SporeCoop/badges'])  # (badges: BUG-0786)


def forget_store():
    """E1 (BUG-1149): every test starts without the per-player store an earlier test left on the host's disk
    (<profile>/Spore/Games-sporecoop-store: one file per save slot and planet, and the profile's salt); a test across
    sessions keeps it through session_restart(), which does not call this"""
    for d in (lane.HOST_DIR, lane.GUEST_DIR):
        subprocess.run(['rm', '-rf', d + '/Games-sporecoop-store'])


def forget_settings():
    """BUG-0762: every test starts with the default session settings: no world keeps the values an earlier test
    stored (<profile>/Spore/SporeCoop/settings, one file per world; a warm reset loads the same planet name)"""
    for d in (lane.HOST_DIR, lane.GUEST_DIR):
        subprocess.run(['rm', '-rf', d + '/SporeCoop/settings'])


# BUG-0430: switches and knobs live in the game process: coop.leave and a load keep them. A test that sets one
# (`coop.fix x off`, `coop.worldfix retry 0`, `frame.limit 50`, `coop.diag sync force shift`, ...) and fails or
# ends before it sets it back (or never does) left them changed for the next test of the lane, which then ran
# warm on games that were not at their start. Every console call that sets one is noted in the lane state
# ('knobs'); a warm start refuses such games (a cold start sets everything back). Each pattern needs a value
# argument: a bare query (`coop.fix list`, `coop.worldfix`) changes nothing. One-shot actions that change no
# setting (`coop.diag sync now`, `frame.limit auto`) are not knobs. start_state() is the second line of defence:
# the game's own report of its switches, compared with the cold start's.
KNOBS = re.compile(r'\s*(?:'
                   r'(?:coop|kit)\.fix\s+\S+\s+\S+'  # coop.fix <name> on|off, coop.fix all off, kit.fix <name> 0
                   r'|coop\.\w+fix\s+\S+|coop\.settingsin\s+\S+'  # coop.worldfix retry 0, coop.framefix 0, coop.settingsin 0
                   r'|coop\.\w+\s+\w*fix\w*\s+\S+'  # coop.pool fix purge 0, coop.npc reusefix 0, coop.space starfix 0
                   r'|coop\.(?:npc|team|savestrip|echo|overlay|sneak)\s+(?:on|off|0|1|force)\b'
                   r'|coop\.settings\s+(?:set|force)\s+\S+\s+\S+|coop\.dna\s+pace\s+\S+'  # BUG-0762: session settings
                   r'|coop\.puppet\s+hold\s+\S+|coop\.track\s+(?:novel|lift)\s+\S+|coop\.npctrack\s+novel\s+\S+'
                   r'|coop\.space\s+repairdist\s+\S+'  # w49-warm: a warm successor would inherit these three
                   r'|coop\.autosave\s+interval\s+\S+'  # E2: the autosave's test interval
                   r'|coop\.uistate\s+(?:lang|color|nudge|hide)\s+\S+|coop\.lanfake\b'
                   r'|coop\.creationprops\s+plant\b'  # BUG-1328: a list under key 0 in the game (removed at the test end)
                   r'|coop\.view\s+set\b'  # a fixed camera (coop_same_view.py): off at the test's end, a crash leaves it
                   r'|coop\.diag\s+(?:sync\s+(?!now\b)\S+|detail\s+\S+|reserve\s+\S+)|coop\.log\s+detail\s+\S+'
                   r'|frame\.limit\s+(?!auto\b)\S+|frames\.source\s+(?!auto\b)\S+'
                   r')')
_knob_lock = threading.Lock()


def note_knob(_con, line):
    """sporectl.Console.on_call: a call that sets a switch or knob goes into the lane state (BUG-0430)"""
    if not KNOBS.match(line):
        return
    with _knob_lock:
        st = lane_state()
        knobs = st.get('knobs', [])
        if line.strip() not in knobs:
            lane_state(**{**st, 'knobs': knobs + [line.strip()]})


Console.on_call = note_knob


def start_state(con):
    """the game's own report of its switches and settings (BUG-0430): coop.diag's `settings:` line (coop.ini values
    that differ from the defaults and every coop.fix switch that is off) and SporeKit's kit.fix list"""
    try:
        diag = con.call('coop.diag').get('out') or ''
        kit = con.call('kit.fix').get('out') or ''
    except (OSError, ValueError):
        return None
    lines = [ln for ln in diag.splitlines() if ln.startswith('settings:')]
    return '\n'.join(lines + kit.strip().splitlines())


def warm_refusal(st, host=None, guest=None):
    """why these games may not run the next test warm (BUG-0312, BUG-0430), '' when they may"""
    if st.get('fix_off'):
        return f'SPORE_FIX_OFF {st["fix_off"]} left switches off'
    if st.get('fps'):
        return f'SPORE_FPS {st["fps"]} left a frame cap'
    if st.get('knobs'):
        return 'an earlier test set ' + ', '.join(f'`{k}`' for k in st['knobs'][:4]) + \
            (f' and {len(st["knobs"]) - 4} more' if len(st['knobs']) > 4 else '')
    want = st.get('start_state') or {}
    for who, con in (('host', host), ('guest', guest)):
        if con is not None and want.get(who) is not None:
            now = start_state(con)
            if now != want[who]:
                diff = sorted(set((now or '').splitlines()) ^ set(want[who].splitlines()))
                return f'the {who}\'s switches differ from its cold start ({"; ".join(diff)[:200]})'
    return ''


def clear_faults(**cons):
    """BUG-1097: the exception logger's count (`exc`) lives in the game process: a warm reset kept it, and the next
    test's Checks.faults() counted the faults an earlier test made on purpose (kit_cmd_fault, kit_present_fault,
    kit_overlay_fault: reports=True, they check their own) as its own. The test before has read and judged them; a warm
    reset starts the count at 0 like a cold start (faults of the reset itself count for the new test). Returns
    {name: the count cleared}. SPORE_WARM_KEEP_FAULTS=1: the old way (the count goes on; the twin of BUG-1097)"""
    if os.environ.get('SPORE_WARM_KEEP_FAULTS') == '1':
        return {}
    cleared = {}
    for name, con in cons.items():
        out = (con.call('exc clear').get('out') or '').splitlines() or ['']
        m = re.search(r'total faults: (\d+)', out[0])
        cleared[name] = int(m.group(1)) if m else 0
    if any(cleared.values()):
        print('  (warm reset: the faults of the tests before cleared: '
              + ', '.join(f'{n} {c}' for n, c in cleared.items()) + ')', flush=True)
    return cleared


# D2 (docs/plan-0.7.15.md, audit TST-4; docs/06-testing.md#testing-warm-fingerprint): the warm contract. A list of what
# to reset (warm_reset, forget_*, KNOBS, clear_faults) grows with every leak; the fingerprint asks the games instead.
# `coop.testreset` resets every session and world state the modules registered (mods/coop/lifecycle.h) and prints what
# still holds something; with the digests of the profile files the tests depend on (PROFILE_FILES: the own-creation
# choices and per-world records, BUG-1176, and My Creations) that is the fingerprint. The cold start records it (the
# games in their galaxy menu, before the load and the join; the files after the start's clean-up); a warm reset takes
# it after the leave and refuses the warm reuse on any difference, naming the entries ("(no warm reuse: the state
# after the reset differs from the cold start: guest test.planted_leftover cold - now 00000007; cold start)").
# SPORE_WARM_FINGERPRINT_FIX=0: no comparison (the twin: the old warm reset).
PROFILE_FILES = ('SporeCoop/choices.ini', 'SporeCoop/worlds.ini', 'EditorSaves.package')
LAST_WARM = {}  # the last _start's warm decision: {'warm': bool, 'why': str, 'fp_s': seconds the fingerprint took}


def _ini_digest(data):
    """the entries of an ini file with a value (section/key=value, case-insensitive names, sorted): an empty file, one
    with empty sections and none at all are the same state (WritePrivateProfileString deletes a key, not its section)"""
    import hashlib
    text = data.decode('utf-16') if data[:2] in (b'\xff\xfe', b'\xfe\xff') else data.decode('latin-1')
    sec, entries = '', []
    for ln in text.splitlines():
        ln = ln.strip()
        if ln.startswith('[') and ln.endswith(']'):
            sec = ln[1:-1].strip().lower()
        elif '=' in ln and not ln.startswith(';'):
            k, v = ln.split('=', 1)
            if v.strip():
                entries.append(f'{sec}/{k.strip().upper()}={v.strip()}')
    return hashlib.sha1('\n'.join(sorted(entries)).encode()).hexdigest()[:12] if entries else 'none'


def file_digest(path):
    """'missing', an ini's entries (_ini_digest) or a file's sha1 (12 hex digits)"""
    import hashlib
    try:
        data = open(path, 'rb').read()
    except OSError:
        return 'missing' if not path.endswith('.ini') else 'none'
    return _ini_digest(data) if path.endswith('.ini') else hashlib.sha1(data).hexdigest()[:12]


def profile_fingerprint():
    """{'<who> <file>': digest} of PROFILE_FILES in both profiles of the lane"""
    return {f'{who} {rel}': file_digest(os.path.join(d, rel)) for who, d in (('host', lane.HOST_DIR), ('guest', lane.GUEST_DIR))
            for rel in PROFILE_FILES}


def game_fingerprint(con, reset=True):
    """{entry: digest} of what the game's registered state still holds after `coop.testreset` (reset=False: `fp`, no
    reset); None when the game has no such command (an older DLL)"""
    try:
        out = con.call('coop.testreset' + ('' if reset else ' fp')).get('out') or ''
    except (OSError, ValueError):
        return None
    if not out.startswith('testreset:'):
        return None
    return dict(ln.split()[:2] for ln in out.splitlines()[1:] if ln.startswith('  ') and len(ln.split()) >= 2)


def fingerprint_now(host, guest, reset=True):
    return {'host': game_fingerprint(host, reset), 'guest': game_fingerprint(guest, reset), 'files': profile_fingerprint()}


def fingerprint_diff(want, now):
    """['<who> <entry> cold <a> now <b>', ...] where `now` differs from the cold start's `want` (a part either side
    lacks, an older DLL, is not compared)"""
    diff = []
    for part in ('host', 'guest', 'files'):
        a, b = (want or {}).get(part), (now or {}).get(part)
        if a is None or b is None:
            continue
        for k in sorted(set(a) | set(b)):
            if a.get(k, '-') != b.get(k, '-'):
                diff.append(f'{"" if part == "files" else part + " "}{k} cold {a.get(k, "-")} now {b.get(k, "-")}')
    return diff


def warm_reset(host, guest, fixture, planet, fingerprint=False):
    """the running games of the lane start the test again: the fixture goes into a fresh save
    slot (the loaded one must not change on disk), the host loads it, the guest joins again.
    fingerprint (D2, _start): after the leave the games' state and the profile files are compared with the cold
    start's (lane state `fingerprint`); on a difference nothing is loaded and None is returned (a refusal, not a
    failure: the caller starts the lane cold)"""
    t0 = time.time()
    st = lane_state()
    slot = 10 + (st.get('slot', 9) - 9) % 10
    clear_faults(host=host, guest=guest)
    for c in (guest, host):
        c.call('coop.leave')
        # BUG-0898: as a cold start, the co-op window closed (a question of the last test opened it); BUG-1139: and on
        # the lobby page (a closed window keeps its Space / settings page for the next F9: space_trade after
        # space_rivalry waited for the lobby's Space button). An older DLL reads `reset` as 0: closed only
        c.call('coop.uistate reset')
    if not KEEP_HOLDS[0]:  # BUG-0679: no hold record of an earlier test (its file and its memory)
        for c in (guest, host):
            c.call('coop.space trade forget')
        forget_holds()
    forget_settings()
    forget_store()
    if fingerprint and os.environ.get('SPORE_WARM_FINGERPRINT_FIX') != '0':
        t1 = time.time()
        now = fingerprint_now(host, guest)
        diff = fingerprint_diff(st.get('fingerprint'), now)
        LAST_WARM['fp_s'] = round(time.time() - t1, 3)
        if diff:
            LAST_WARM['why'] = 'the state after the reset differs from the cold start: ' + '; '.join(diff)
            print(f'  (no warm reuse: {LAST_WARM["why"][:600]}; cold start)', flush=True)
            return None
        if st.get('fingerprint') is None:
            print('  (warm reset: no fingerprint of the cold start to compare with)', flush=True)
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
    settled = startwait.settle([host, guest], 3)  # A9: was a fixed 3 s
    phases.append(('settled', time.time() - t0))
    print(f'  (warm reset in {time.time() - t0:.0f} s: ' + ', '.join(f'{n} {t:.0f}' for n, t in phases)
          + f'; warm run {st.get("warm_runs", 0) + 1} of these games; settle '
          + (f'{settled:.1f} s' if settled is not None else 'at the 3 s bound')
          + (f'; fingerprint {LAST_WARM["fp_s"]:.2f} s, same as the cold start' if fingerprint and 'fp_s' in LAST_WARM else '')
          + ')', flush=True)
    return True


def played_slot():
    """('GameN', 'Planet') the host plays now: the cold start's fixture planet, or the slot of the last warm reset"""
    st = lane_state()
    planet = st.get('planet') or ''
    if '/' not in planet:
        return None, None
    slot, name = planet.split('/', 1)
    if st.get('slot', 9) != 9:
        slot = f'Game{st["slot"]}'
    return slot, name


def fix_off_names():
    return [n for n in (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',')) if n]


def apply_fix_off(con, who):
    """SPORE_FIX_OFF in a game started again by the test (start() did it for the first games).
    BUG-1314: right after a start (wait_up: SporeKit's console answers) the mod's commands may not be there yet
    ("unknown command: coop.fix"): the twins of space_own_ship_key, own_editor_key and late_species ran their restarted
    guest with the fix on. Now it asks again until the answer says the switch is off (at most 90 s) and fails the test
    as start() does when it never does."""
    for name in fix_off_names():
        cmd = f'kit.fix {name[4:]} 0' if name.startswith('kit.') else f'coop.fix {name} off'
        t0, out = time.time(), ''
        while True:
            out = (con.call(cmd).get('out') or '').strip()
            if fix_off_ok(name, out) or time.time() - t0 > 90:
                break
            time.sleep(1)
        print(f'  SPORE_FIX_OFF ({who}, restarted): ' + out, flush=True)
        if not fix_off_ok(name, out):
            print(f'RESULT: FAIL (SPORE_FIX_OFF ({who}, restarted): {name} not off: '
                  f'{out.splitlines()[0] if out else "no answer"})', flush=True)
            raise SystemExit(1)


def save_taken(con, secs=30.0, label='host save'):
    """`coop.save` until the game takes the save: the mod refuses a save "for now" while the game would drop it (BUG-1365:
    its last save still finishing, 1.5 s after that box's OK; BUG-0846: that box still up; an editor or a mode switch
    on the way) and says why. Asks again every 0.5 s for at most secs, prints each
    new refusal; returns (answer, refusals, asked): answer starts with "saved (mode" when the save was posted, asked is the
    time.time() of the last ask (the one the game took) (BUG-1398)."""
    t0, said, refused, last = time.time(), '', 0, None
    while True:
        asked = time.time()
        said = (con.call('coop.save').get('out') or '').strip()
        if said.startswith('saved (mode') or time.time() - t0 > secs:
            return said, refused, asked
        refused += 1
        if said != last:
            print(f'  {label} refused for now ({time.time() - t0:.1f} s): {said[:120]}', flush=True)
            last = said
        time.sleep(0.5)


def save_fresh(profile_dir, since, secs=30.0):
    """the game wrote a save after `since` (time.time()): the newest planet file (Games/*/*.spo) of the profile is newer.
    A check that parses "the newest save" reads this one and not an older save (BUG-1398)."""
    import glob
    t0 = time.time()
    while True:
        fs = glob.glob(os.path.join(profile_dir, 'Games', '*', '*.spo'))
        try:
            if fs and max(os.path.getmtime(f) for f in fs) >= since:
                return True
        except OSError:
            pass
        if time.time() - t0 > secs:
            return False
        time.sleep(0.5)


def session_restart(host, guest, save=True, store_wait=20.0, slot=None, planet=None, before_join=None,
                    host_kill=False):
    """A new evening (E1 of 0.7.15, BUG-1149; docs/06-testing.md#testing-session-restart): both players quit, the
    host's game starts again and loads its own save of the world, the guest's game starts again and joins.
    Nothing of the lane is restored or forgotten in between: the host's profile keeps its save slot and its per-player
    store (Games-sporecoop-store), the guest's profile its kept rejoin token (its clean leave, BUG-1135).

    1. save (default): the host saves its game (`coop.save`, the game's own save into its own slot, like the pause
       menu's Save; asked again while the mod refuses it for now, save_taken) and waits for the per-player store's
       write (`coop.store`: writes > before, at most store_wait s); info['saved'] only when both happened (BUG-1398);
    2. the guest leaves (`coop.leave`: a clean leave), then the host;
    3. both games end (`spore.sh kill`: no other process of the lane runs on; host_kill only says in the log that the
       host's end was a crash, the end is the same);
    4. both start cold; SPORE_FIX_OFF is set in both again; the host loads `slot`/`planet` (default: played_slot())
       and hosts; the guest joins (before_join(guest) right before) and loads the host's world.
    Returns (host, guest, info): the new consoles and {'saved': bool, 'store_writes': n, 'seconds': t}. The games are
    no warm games for the next test (lane state knobs: a session restart)."""
    t0 = time.time()
    info = {'saved': False, 'store_writes': None}
    if slot is None or planet is None:
        s, p = played_slot()
        slot, planet = slot or s, planet or p
    assert slot and planet, 'session_restart: which save the host plays is not known (lane state)'

    def writes(con):
        m = re.search(r'writes=(\d+)', con.call('coop.store').get('out') or '')
        return int(m.group(1)) if m else -1
    if save:
        w0 = writes(host)
        # BUG-1398: a save right after another one (species_line: 1.5 s after the rejoin's world-share save) was dropped by
        # the game (BUG-1365) and coop.save still said "saved": the store's write never came (writes 2 -> 2 in every run)
        # and the next session loaded the share's save. Since BUG-1365 the mod refuses it for now: ask again until the
        # game takes it, and count the save only when the game really made it (the store is written by the save).
        out, refused, _ = save_taken(host, 30, 'session_restart: host save')
        print(f'  session_restart: host save: {out[:120]}' + (f' (after {refused} refusals)' if refused else ''),
              flush=True)
        posted = out.startswith('saved')
        t1 = time.time()
        while time.time() - t1 < store_wait:
            f1 = host_frame(host)
            time.sleep(0.5)
            if host_frame(host) == f1:
                host.call('key enter')  # the game's "game saved" box holds the frames
            if writes(host) > w0:
                break
        info['store_writes'] = writes(host)
        info['saved'] = posted and info['store_writes'] > w0  # BUG-1398: posted and made (a dropped save writes nothing)
        print(f'  session_restart: store writes {w0} -> {info["store_writes"]} '
              f'({(host.call("coop.store").get("out") or "").splitlines()[0][:200]})', flush=True)
        time.sleep(1)
    for m in _monitors:
        m.stop = True
    try:
        guest.call('coop.leave')
        time.sleep(2)
        host.call('coop.leave')
        time.sleep(1)
    except OSError:
        pass
    print('  session_restart: both games end' + (' (the host as a crash)' if host_kill else ''), flush=True)
    sh('kill')
    lane_down(10)
    lane_state(**{**lane_state(), 'knobs': lane_state().get('knobs', []) + ['(session_restart)']})
    menu = startwait.MenuFrames()
    lane.run_guest()
    game_process_up(lane.GUEST)
    lane.run_host()
    for inst in (lane.GUEST, lane.HOST):
        assert wait_up(inst, 120), f'session_restart: instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    t1 = time.time()
    while time.time() - t1 < 180:
        if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            menu.note('guest', guest)
        if 'GalaxyGameEntry' in host.call('game.mode')['out']:
            break
        time.sleep(startwait.STEP)
    host.call('wait.frames 240 90000')
    apply_fix_off(host, 'host')
    apply_fix_off(guest, 'guest')
    apply_fps(host, 'host')
    apply_fps(guest, 'guest')
    host.call(f'coop.load {slot} {planet}')
    t1 = time.time()
    while time.time() - t1 < 240 and not host.call('coop.avatar')['ok']:
        time.sleep(startwait.STEP)
    startwait.settle([host], 4)
    host_up(host)
    for _ in range(240):
        if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            menu.note('guest', guest)
            break
        time.sleep(0.5)
    menu.wait_menu('guest', guest, 200)
    if before_join is not None:
        before_join(guest)
    guest.call(lane.JOIN)
    if not wait_guest_in(host, guest):
        print('  (session_restart: the guest is not in the host\'s world after 240 s)', flush=True)
    startwait.settle([host, guest], 4)
    watch(lane.HOST, lane.GUEST)
    PLAYERS.clear()
    PLAYERS.update(host=host, guest=guest)
    info['seconds'] = time.time() - t0
    print(f'  session_restart: a new session in {info["seconds"]:.0f} s ({slot}/{planet})', flush=True)
    return host, guest, info


def start(*a, **kw):
    """_start(), then SPORE_FIX_OFF=<name>[,<name>...] switches those fixes off on both games
    (`coop.fix <name> off`, BUG-0116): any test can run once without a fix to show that it still
    catches the bug. Fixes that act while the guest joins are on until then.
    Strict (BUG-0214 R8): a name the game does not know fails the test at once, so a twin run
    cannot pass silently with the fix still on; an unsafe switch only gets a warning line."""
    KEEP_HOLDS[0] = kw.pop('keep_holds', False)
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
    PLAYERS.clear()
    PLAYERS.update(host=host, guest=guest)
    lane.on_kill = (_before_kill, _after_kill)  # BUG-1329: from here on a killed game's faults count
    return host, guest


# BUG-1329: a test that kills a game and starts it again (lane.sh('kill'), coop_creature_desync.kill_game:
# civ_late_arrival's restarted guest) lost that instance's faults and log: Checks.faults() asked the games alive at the
# end and the lab kept the last instance's log, so civ_late_arrival passed while every restarted guest's City Hall
# editor ended in an access violation (lab 20261119-113002, 20261119-130049, 20261119-201729). After start() each game
# a test kills has its `exc` read first (KILLED) and its log copied into the lab's run folder
# (<test>.<who>.killed<N>.game.log); Checks.faults() judges a killed instance as it judges the live ones (a game that
# did not answer before its kill is only noted). SPORE_KILLED_FAULTS=0: the old way (the twin of this harness fix).
KILLED = []  # [(name, the `exc` lines, None when it did not answer)]
_kills = {}  # {who: kills so far}


def _who(inst):
    return 'host' if inst == lane.HOST else 'guest' if inst == lane.GUEST else f'inst{inst}'


def _before_kill(insts):
    if os.environ.get('SPORE_KILLED_FAULTS') == '0':
        return
    for inst in insts:
        try:
            out = (Console(inst, timeout=5).call('exc').get('out') or '').splitlines() or ['']
        except (OSError, ValueError):
            out = None
        if out is None and not os.path.exists(lane.game_log(inst)):
            continue
        who = _who(inst)
        _kills[who] = _kills.get(who, 0) + 1
        KILLED.append((f'{who} (killed instance {_kills[who]})', out))


def _after_kill(insts):
    run, test = os.environ.get('SPORE_LAB_RUN'), os.environ.get('SPORE_LAB_TEST')
    if os.environ.get('SPORE_KILLED_FAULTS') == '0' or not run or not test:
        return
    for inst in insts:
        who = _who(inst)
        src = lane.game_log(inst)
        if who in _kills and os.path.exists(src):
            try:
                import shutil
                shutil.copy(src, os.path.join(run, f'{test}.{who}.killed{_kills[who]}.game.log'))
            except OSError:
                pass


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


def _menu_up_time(inst):
    """time.time() of the last `stage: ... -> Galaxy menu` line in the game's log (the moment the mod saw the menu
    come up; game.mode answers GalaxyGameEntry up to ~12 s later on a cold start), or None"""
    import datetime
    try:
        with open(lane.game_log(inst), errors='replace') as f:
            lines = [ln for ln in f if '-> Galaxy menu' in ln]
    except OSError:
        return None
    m = re.match(r'(\d\d):(\d\d):(\d\d)\.(\d+)', lines[-1]) if lines else None
    if not m:
        return None
    now = datetime.datetime.now()
    t = now.replace(hour=int(m.group(1)), minute=int(m.group(2)), second=int(m.group(3)),
                    microsecond=int(m.group(4).ljust(6, '0')[:6]))
    if t > now + datetime.timedelta(minutes=1):
        t -= datetime.timedelta(days=1)
    return t.timestamp()


def _start(fixture='creature', planet=None, load_args=(), join_in_intro=False, guest_creations=True, join_at=None,
           before_join=None):
    """kill, deploy, restore the fixture for the host, empty guest profile, start both,
    load the host game (tools/tests/load_game.py), host + join; returns (host, guest).
    With SPORE_WARM=1 (tools/lab.py) the running games of the lane are reused when they run the
    same build: warm_reset() instead of a restart (~10 s instead of ~40 s).
    join_in_intro: the guest joins while its galaxy menu plays the intro cinematic GGE_Intro (cold
    start only; the race behind the stuck guest movie, docs/re/space.md 7.9).
    join_at: the guest joins this many seconds after its galaxy menu came up (polled every 0.25 s; cold
    start only), so that its load comes at a chosen moment of the menu's intro timer (BUG-0607).
    before_join: called with the guest's console right before it joins (cold start only; BUG-0676: the guest's
    first galaxy menu set up for the test)."""
    if planet is None and not load_args:
        planet = fixtures.planet_of(fixture)  # loaded by name (no menu clicks, warm-capable)
    st = lane_state()
    # warm only within the same save: a test starts from its fixture (stage switches in game work,
    # coop_stage_switch.py, but go through the galaxy menu and take longer)
    if os.environ.get('SPORE_WARM') == '1' and planet and not join_in_intro and join_at is None and before_join is None \
            and st.get('build') == build_id() \
            and st.get('fixture') == fixture and st.get('guest_creations', True) == guest_creations \
            and not PRESET_CHOICES and alive(lane.HOST) and alive(lane.GUEST):
        host, guest = Console(lane.HOST), Console(lane.GUEST)
        why = ''
        LAST_WARM.clear()
        try:
            why = warm_refusal(st, host, guest)
            if why:
                print(f'  (no warm reuse: {why}; cold start)', flush=True)
            elif (ok := warm_reset(host, guest, fixture, planet, fingerprint=True)) is None:
                why = LAST_WARM.get('why') or 'fingerprint'  # D2: printed by warm_reset
            elif ok:
                watch(lane.HOST, lane.GUEST)
                if check_fixture(host, fixture):
                    LAST_WARM['warm'] = True
                    return host, guest
                for m in _monitors:  # the reused games miss a fixture fact: start them again from the files
                    m.stop = True
                print('  (warm reset: the fixture lacks its facts)', flush=True)
        except (OSError, ValueError) as e:
            print('  (warm reset failed:', e, ')', flush=True)
        if not why:
            print('  (warm reset failed: cold start)', flush=True)
        LAST_WARM.update(warm=False, why=why or LAST_WARM.get('why') or 'the warm reset failed')
    lane_state()  # (the state is rewritten below once the games run)
    sh('kill')
    lane_down()  # A9: was a fixed 2 s
    sh('deploy')
    sh('fixture', 'restore', fixture, lane.HOST_PROFILE)
    restore_creations(guest_creations)
    subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
    if not KEEP_HOLDS[0]:
        forget_holds()
    forget_settings()
    forget_store()
    # own-creation choices and the per-world records of the own creature (worlds.ini, BUG-0345; BUG-1176: a record an
    # earlier test left decided whether the guest played its own creature in the fixture world): back to the fixture
    # state, none; a test that needs the own creature chooses it (creations_state.choose_own). SPORE_WORLDS_INI_RESET_FIX=0:
    # the old start (worlds.ini kept), the twin of BUG-1176
    for d in (lane.HOST_DIR, lane.GUEST_DIR):
        subprocess.run(['rm', '-f', d + '/SporeCoop/choices.ini']
                       + ([] if os.environ.get('SPORE_WORLDS_INI_RESET_FIX') == '0' else [d + '/SporeCoop/worlds.ini']))
    # D2: the profile files of the fixture state (a test's PRESET_CHOICES below are not: its warm successor starts cold)
    fp_cold = {'host': None, 'guest': None, 'files': profile_fingerprint()}
    for who, lines in PRESET_CHOICES.items():  # a test's choices from an earlier session (BUG-0342/0344)
        d = os.path.join(lane.HOST_DIR if who == 'host' else lane.GUEST_DIR, 'SporeCoop')
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'choices.ini'), 'w', newline='\r\n') as f:
            f.write('[creations]\n' + ''.join(f'{k}={v}\n' for k, v in lines.items()))
    t_start = time.time()
    menu = startwait.MenuFrames()  # A9: frames each game has drawn in its galaxy menu
    # join_in_intro: the guest starts once the host is in its world, so that its menu intro comes
    # after the host is ready (the guest joins at the start of it)
    if not join_in_intro:
        lane.run_guest()
        # two sandboxes starting at the very same moment can race on the bottle: the host starts once the guest's
        # game process runs (A9: was a fixed 2 s, the bound)
        game_process_up(lane.GUEST)
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host))[1 if join_in_intro else 0:]:
        if not wait_up(inst, 90):  # two sandboxes starting together sometimes lose one: start it again
            # BUG-0267: the lost one may still be starting (a busy machine): a second copy of the
            # same instance next to it failed to copy coop.dll (err 32) and hung. End the lane's
            # games first, then start both again.
            print(f'  (instance {inst} did not answer in 90 s: the lane starts again)', flush=True)
            sh('kill')
            lane_down()
            if not join_in_intro:
                lane.run_guest()
                game_process_up(lane.GUEST)
            lane.run_host()
            for i in ((lane.HOST,) if join_in_intro else (lane.GUEST, lane.HOST)):
                assert wait_up(i), f'instance {i} did not start'
            break
    phases = [('wine up', time.time() - t_start)]
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    if planet:  # GameN/Planet of the fixture, loaded through the mod (works from the galaxy menu)
        t0 = time.time()
        while time.time() - t0 < 180:
            if not join_in_intro and 'GalaxyGameEntry' in guest.call('game.mode')['out']:
                menu.note('guest', guest)  # A9: the guest's menu frames count from here
            if 'GalaxyGameEntry' in host.call('game.mode')['out']:
                break
            time.sleep(1 if startwait.fixed() else startwait.STEP)  # A9: a 0.25 s poll (was 1 s)
        phases.append(('host menu', time.time() - t_start))
        host.call('wait.frames 240 90000')  # the galaxy menu settles (8 s)
        fp_cold['host'] = game_fingerprint(host)  # D2: what a fresh game holds (its galaxy menu, before the load)
        slot, name = planet.split('/')
        host.call(f'coop.load {slot} {name}')
        t0 = time.time()
        while time.time() - t0 < 240 and not host.call('coop.avatar')['ok']:
            time.sleep(2 if startwait.fixed() else startwait.STEP)  # A9: a 0.25 s poll (was 2 s)
        phases.append(('host in game', time.time() - t_start))
        # A9: was a fixed 4 s; the host's frames move and its game time runs (the stage entry's pause is over)
        startwait.settle([host], 4)
        phases.append(('host settled', time.time() - t_start))
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
    for _ in range(120 * (4 if join_at is not None else 1)):
        if join_at is not None:  # the log's line comes when the menu does; game.mode answers up to ~12 s later
            if (_menu_up_time(lane.GUEST) or 0) > t_start:  # this start's line, not an older session's
                break
        elif 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            menu.note('guest', guest)
            break
        time.sleep(1 if join_at is None and startwait.fixed() else 0.25)
    t_menu = time.time()
    phases.append(('guest menu', time.time() - t_start))
    if join_at is not None:
        # SPORE_FIX_OFF before the join too: a fix of the join itself is off when it is needed
        for name in (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',')):
            if name and not name.startswith('kit.'):
                print('  SPORE_FIX_OFF (guest, before the join): ' + guest.call(f'coop.fix {name} off')['out'].strip(),
                      flush=True)
        t_menu = _menu_up_time(lane.GUEST) or t_menu
        time.sleep(max(0.0, t_menu + join_at - time.time()))
    else:
        # A9: 200 frames of the guest's menu, counted from the first poll that saw it (often long done: the guest's
        # menu came up while the host loaded); was `wait.frames 200` from here
        menu.wait_menu('guest', guest, 200)
    if join_in_intro:  # the intro starts ~16 s after the menu came up and runs ~10 s
        for _ in range(120):
            if '(menu intro)' in guest.call('coop.status')['out']:
                break
            time.sleep(0.25)
        else:
            print('  (the guest menu intro did not start)', flush=True)
    if join_at is None:
        fp_cold['guest'] = game_fingerprint(guest)  # D2: the guest's fresh game, before the join
    if before_join is not None:
        before_join(guest)
    phases.append(('guest join', time.time() - t_start))
    guest.call(lane.JOIN)
    # the host saves before sharing and the game shows a modal "game saved" box (frames stop):
    # close it whenever it is up, until the guest is in the world
    if not wait_guest_in(host, guest):
        print('  (the guest is not in the host\'s world after 240 s: '
              + guest.call('coop.status')['out'].strip().splitlines()[0][:160] + ')', flush=True)
    phases.append(('guest in game', time.time() - t_start))
    settled = startwait.settle([host, guest], 4)  # A9: was a fixed 4 s
    print(f'  (both games ready in {time.time() - t_start:.0f} s: '
          + ', '.join(f'{n} {t:.0f}' for n, t in phases) + '; settle '
          + (f'{settled:.1f} s' if settled is not None else 'at the 4 s bound') + ')', flush=True)
    lane_state(build=build_id(), fixture=fixture, planet=planet, slot=9, warm_runs=0, guest_creations=guest_creations,
               start_state={'host': start_state(host), 'guest': start_state(guest)},
               fingerprint=fp_cold)
    watch(lane.HOST, lane.GUEST)
    check_fixture(host, fixture)
    return host, guest


# BUG-0967: a fault of the game's audio init (0x011ECE30, a constructor reading its third argument's +0x120, null:
# 0x011EA810 <- the rw::audio core, Snd9Service) right after App::cSporeApp::Init, before the galaxy menu and any test
# step; handled by the game. Seen twice in all lab runs (20261101-143943, 20261112-135150: host and guest at once).
# SPORE_BOOT_FAULT_STRICT=1 counts it again.
BOOT_AUDIO_FAULT = re.compile(r'0xC0000005 x(\d+) at 0x011ECE4F \S+ data=0x00000120 ')


class Checks:
    def __init__(self):
        self.ok = True

    def __call__(self, cond, msg):
        harness.check_line(cond, msg)  # the ok / FAIL line convention (D4, tools/tests/harness.py)
        self.ok &= bool(cond)
        return cond

    def faults(self, **cons):
        outs = [(name, con.call('exc')['out'].splitlines() or ['']) for name, con in cons.items()]
        for name, out in KILLED:  # BUG-1329: the games the test killed on its way
            if out is None:
                print(f'{name}: it did not answer before its kill (its faults are not known)')
            else:
                outs.append((name, out))
        for name, out in outs:
            m = re.search(r'total faults: (\d+)', out[0])
            known = sum(int(k.group(1)) for line in out[1:] for k in [BOOT_AUDIO_FAULT.match(line.strip())] if k)
            if m and known and int(m.group(1)) == known and os.environ.get('SPORE_BOOT_FAULT_STRICT') != '1':
                # BUG-0967: only the game's own audio init (rw::audio, Snd9Service) at boot, before any test step,
                # handled by the game: printed without the "total faults: N" the lab counts
                print(f'{name} faults: none but {known} known one(s) of the game\'s audio init at boot (BUG-0967)')
                for line in out[1:3]:
                    print(f'    {line.strip()}')
                continue
            print(f'{name} faults: {out[0]}')
            for line in out[1:4]:
                print(f'    {line.strip()}')
            self.ok &= 'total faults: 0' in out[0]
        for e in Monitor.events + FIXTURE_BAD:  # freezes / crashes seen while the test ran, a broken fixture
            print('  FAIL invariant:', e)
            self.ok = False

    def result(self):
        monitor_flush()
        return harness.result_line(self.ok)

    def inconclusive(self, why):
        """A15: the test's precondition did not happen (`RESULT: INCONCLUSIVE <why>`, never a pass); a check that failed
        before still makes it RESULT: FAIL"""
        if not self.ok:
            return self.result()
        monitor_flush()
        return harness.inconclusive(why)
