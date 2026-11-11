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

memprobe.start()  # SPORE_MEMPROBE=1|trace: the test script's own memory (BUG-0916)


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


def keyframe(tag, note, players=None, kind='key', extra=None):
    """a key frame (docs/06-testing.md#testing-key-frames): every game of the test is shot at the same moment
    (one thread each), stored as a ~960 px JPEG in <run>/frames/<test>/<nn>_<tag>_<role>.jpg with a manifest line
    (the note says what should be visible; each game's mode and position). players: {role: Console}, default the
    host and guest of start(); kind 'key', 'phase' (a campaign's phase end) or 'sameview'. Never fails the test:
    a game that does not answer gets ok=false in the manifest. Returns the manifest entry (None on an error)."""
    import frames
    try:
        players = dict(players or PLAYERS)
        if not players:
            return None
        d = frames.frames_dir(lane.out_dir())
        nn = frames.next_number(d)
        tmp = lane.out_dir('shots', 'keyframes')
        games = {}

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
        subprocess.run(['rm', '-rf', d + '/SporeCoop/holds'])


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
                   r'|coop\.\w+fix\s+\S+'  # coop.worldfix retry 0, coop.corefix <name> 0, coop.sessionfix 0, coop.framefix 0
                   r'|coop\.\w+\s+\w*fix\w*\s+\S+'  # coop.pool fix purge 0, coop.npc reusefix 0, coop.space starfix 0
                   r'|coop\.(?:npc|team|savestrip|echo|overlay|sneak)\s+(?:on|off|0|1|force)\b'
                   r'|coop\.settings\s+(?:set|force)\s+\S+\s+\S+|coop\.dna\s+pace\s+\S+'  # BUG-0762: session settings
                   r'|coop\.puppet\s+hold\s+\S+|coop\.track\s+novel\s+\S+|coop\.uistate\s+(?:lang|color|nudge|hide)\s+\S+|coop\.lanfake\b'
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


def warm_reset(host, guest, fixture, planet):
    """the running games of the lane start the test again: the fixture goes into a fresh save
    slot (the loaded one must not change on disk), the host loads it, the guest joins again"""
    t0 = time.time()
    st = lane_state()
    slot = 10 + (st.get('slot', 9) - 9) % 10
    for c in (guest, host):
        c.call('coop.leave')
        c.call('coop.uistate 0')  # BUG-0898: as a cold start, the co-op window closed (a question of the last test opened it)
    if not KEEP_HOLDS[0]:  # BUG-0679: no hold record of an earlier test (its file and its memory)
        for c in (guest, host):
            c.call('coop.space trade forget')
        forget_holds()
    forget_settings()
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
        try:
            why = warm_refusal(st, host, guest)
            if why:
                print(f'  (no warm reuse: {why}; cold start)', flush=True)
            elif warm_reset(host, guest, fixture, planet):
                watch(lane.HOST, lane.GUEST)
                if check_fixture(host, fixture):
                    return host, guest
                for m in _monitors:  # the reused games miss a fixture fact: start them again from the files
                    m.stop = True
                print('  (warm reset: the fixture lacks its facts)', flush=True)
        except (OSError, ValueError) as e:
            print('  (warm reset failed:', e, ')', flush=True)
        if not why:
            print('  (warm reset failed: cold start)', flush=True)
    lane_state()  # (the state is rewritten below once the games run)
    sh('kill')
    time.sleep(2)
    sh('deploy')
    sh('fixture', 'restore', fixture, lane.HOST_PROFILE)
    restore_creations(guest_creations)
    subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
    if not KEEP_HOLDS[0]:
        forget_holds()
    forget_settings()
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
    for _ in range(120 * (4 if join_at is not None else 1)):
        if join_at is not None:  # the log's line comes when the menu does; game.mode answers up to ~12 s later
            if (_menu_up_time(lane.GUEST) or 0) > t_start:  # this start's line, not an older session's
                break
        elif 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            break
        time.sleep(1 if join_at is None else 0.25)
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
        guest.call('wait.frames 200 60000')
    if join_in_intro:  # the intro starts ~16 s after the menu came up and runs ~10 s
        for _ in range(120):
            if '(menu intro)' in guest.call('coop.status')['out']:
                break
            time.sleep(0.25)
        else:
            print('  (the guest menu intro did not start)', flush=True)
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
    time.sleep(4)
    print(f'  (both games ready in {time.time() - t_start:.0f} s: '
          + ', '.join(f'{n} {t:.0f}' for n, t in phases) + ')', flush=True)
    lane_state(build=build_id(), fixture=fixture, planet=planet, slot=9, warm_runs=0, guest_creations=guest_creations,
               start_state={'host': start_state(host), 'guest': start_state(guest)})
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
