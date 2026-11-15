#!/usr/bin/env python3
"""Self-test of the lab's start-up waits and budget (plan 0.7.15 A9, A16, D1; BUG-1112..1114), no game needed
(make test). Fake consoles, a fake clock and fake /proc / sysfs readings.

  startwait   until(): true at once, true later, never (the old bound, not more), a probe that raises; settle():
              frames that move and game time that runs end it early, a game standing on its pause or a frozen
              counter keep it to the bound; MenuFrames: 200 frames counted from the first poll that saw the menu;
              SPORE_START_FIXED=1 sleeps the old fixed time
  displays    lane N on headless shell (N - 1) % SPORE_HEADLESS_DISPLAYS: labbudget.display_name and
              `headless.sh name` agree for lanes 1-10 with 1, 2 and 3 shells; SPORE_HEADLESS_NAME wins
  budget      decide(): the game budget (18), VRAM, games per display (10), a saturated Xwayland, a load-sensitive test
              (12 games), the lane's own running games (warm: none added), games of tests just started; the Gate
              waits and starts the test when room comes back, fails as not started with nothing running, ends on
              an interrupt; SPORE_LAB_BUDGET=0 switches it off
  fpslimit    fpslimit.scaled and npc_walk_tribe's p50 limit: 0.10 at 30 fps and faster, 3.0 / fps below, 0.20 at most
              (every recorded fix-off FAIL, 0.27-0.29 m, stays over it)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE)))
import subprocess  # noqa: E402
import threading  # noqa: E402
import time  # noqa: E402
import fpslimit  # noqa: E402
import labbudget  # noqa: E402
import startwait  # noqa: E402

FAILS = []


def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what, flush=True)
    if not cond:
        FAILS.append(what)


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t

    def sleep(self, s):
        self.t += s


class FakeGame:
    """a console: `frame` from a frame rate on the fake clock (or frozen), `coop.pauses` paused until a time"""
    def __init__(self, clock, fps=30.0, runs_at=0.0, frozen=False, dead=False):
        self.clock, self.fps, self.runs_at, self.frozen, self.dead = clock, fps, runs_at, frozen, dead
        self.calls = []

    def call(self, line):
        self.calls.append(line)
        if self.dead:
            raise ConnectionRefusedError('no game')
        if line == 'frame':
            f = 100 if self.frozen else 100 + int(self.clock() * self.fps)
            return {'ok': True, 'out': f'frame={f} fps={self.fps:.1f} limit=33 own=0 changes=0'}
        if line == 'coop.pauses':
            return {'ok': True, 'out': f'paused={0 if self.clock() >= self.runs_at else 1} reasons=04BF38A7'}
        if line.startswith('wait.frames'):
            n = int(line.split()[1])
            self.clock.sleep(n / self.fps)
            return {'ok': True, 'out': 'frame=0'}
        return {'ok': False, 'out': ''}


def case_until():
    print('startwait: until()')
    c = Clock()
    w = startwait.until(lambda: True, 2.0, clock=c, sleep=c.sleep)
    check(w == 0.0 and c.t == 0.0, f'a condition true at once: no wait ({w})')
    c = Clock()
    w = startwait.until(lambda: c.t >= 0.6, 2.0, clock=c, sleep=c.sleep)
    check(w is not None and 0.6 <= w <= 0.85, f'true after 0.6 s: returns within one step ({w})')
    c = Clock()
    w = startwait.until(lambda: False, 2.0, clock=c, sleep=c.sleep)
    check(w is None and abs(c.t - 2.0) < 1e-9, f'never true: None at exactly the old bound ({c.t:.2f} s)')
    c = Clock()

    def raises():
        if c.t < 1.0:
            raise ConnectionRefusedError('not up')
        return True
    w = startwait.until(raises, 3.0, clock=c, sleep=c.sleep)
    check(w is not None and 1.0 <= w <= 1.25, f'a probe that raises OSError counts as not yet ({w})')
    os.environ['SPORE_START_FIXED'] = '1'
    try:
        c = Clock()
        w = startwait.until(lambda: True, 4.0, clock=c, sleep=c.sleep)
        check(w is None and c.t == 4.0, 'SPORE_START_FIXED=1: the old fixed sleep, even when the signal is there')
    finally:
        del os.environ['SPORE_START_FIXED']


def case_settle():
    print('startwait: settle()')
    c = Clock()
    h, g = FakeGame(c), FakeGame(c)
    w = startwait.settle([h, g], 4.0, clock=c, sleep=c.sleep)
    check(w is not None and w <= 0.3, f'both draw and run: settled after one step ({w})')
    c = Clock()
    h, g = FakeGame(c), FakeGame(c, runs_at=1.5)
    w = startwait.settle([h, g], 4.0, clock=c, sleep=c.sleep)
    check(w is not None and 1.5 <= w <= 1.8, f'the guest stands on its stage entry pause for 1.5 s ({w})')
    c = Clock()
    h, g = FakeGame(c), FakeGame(c, runs_at=30)
    w = startwait.settle([h, g], 4.0, clock=c, sleep=c.sleep)
    check(w is None and abs(c.t - 4.0) < 1e-9, f'a pause longer than the bound: the old 4 s, not more ({c.t:.2f})')
    c = Clock()
    h, g = FakeGame(c, frozen=True), FakeGame(c)
    w = startwait.settle([h, g], 3.0, clock=c, sleep=c.sleep)
    check(w is None and abs(c.t - 3.0) < 1e-9, 'a frame counter that does not move (a modal box): to the bound')
    c = Clock()
    h, g = FakeGame(c, dead=True), FakeGame(c)
    w = startwait.settle([h, g], 3.0, clock=c, sleep=c.sleep)
    check(w is None and abs(c.t - 3.0) < 1e-9, 'a game that does not answer: to the bound, no exception')


def case_menu():
    print('startwait: MenuFrames')
    c = Clock()
    g = FakeGame(c, fps=50)
    m = startwait.MenuFrames()
    m.note('guest', g)  # menu seen at t=0
    c.sleep(10)  # the host loads meanwhile: 500 menu frames
    m.note('guest', g)  # a later poll does not move the start
    check(m.left('guest', g, 200) == 0, 'the menu was up for 500 frames: nothing left of the 200')
    check(m.wait_menu('guest', g, 200) == 0 and not any(x.startswith('wait.frames') for x in g.calls),
          'and no wait.frames is sent')
    c = Clock()
    g = FakeGame(c, fps=50)
    m = startwait.MenuFrames()
    m.note('guest', g)
    c.sleep(1)  # 50 frames
    check(m.left('guest', g, 200) == 150, f'50 frames in the menu: 150 left ({m.left("guest", g, 200)})')
    m.wait_menu('guest', g, 200)
    check('wait.frames 150 60000' in g.calls, 'wait.frames for what is left')
    m = startwait.MenuFrames()
    check(m.left('x', g, 200) == 200, 'a menu never seen: the full 200 frames from now (the old wait)')
    m.note('dead', FakeGame(c, dead=True))
    check('dead' in m.at and m.left('dead', g, 200) == 200, 'seen without a frame answer: the full 200')
    os.environ['SPORE_START_FIXED'] = '1'
    try:
        m = startwait.MenuFrames()
        m.note('guest', g)
        c.sleep(10)
        check(m.left('guest', g, 200) == 200, 'SPORE_START_FIXED=1: the old wait.frames 200 from the join')
    finally:
        del os.environ['SPORE_START_FIXED']


def case_displays():
    print('A16: lanes over the headless displays')
    sh = os.path.join(os.path.dirname(HERE), 'headless.sh')
    env0 = {k: v for k, v in os.environ.items() if k not in ('SPORE_HEADLESS_NAME', 'SPORE_HEADLESS_DISPLAYS')}
    bad = []
    for n in (1, 2, 3):
        for lane in range(1, 11):
            env = dict(env0, SPORE_LANE=str(lane), SPORE_HEADLESS_DISPLAYS=str(n))
            got = subprocess.run(['bash', sh, 'name'], env=env, capture_output=True, text=True).stdout.strip()
            want = labbudget.display_name(lane, n)
            if got != want:
                bad.append((n, lane, got, want))
    check(not bad, f'headless.sh name and labbudget.display_name agree for lanes 1-10 with 1-3 shells ({bad[:2]})')
    check([labbudget.display_name(x, 2) for x in (2, 3, 4, 5)] ==
          ['sporecoop-headless-2', 'sporecoop-headless', 'sporecoop-headless-2', 'sporecoop-headless'],
          'a 4-lane final check (lanes 2-5) has two lanes on each display')
    check(labbudget.display_name(4, 1) == 'sporecoop-headless', 'SPORE_HEADLESS_DISPLAYS=1: one shared shell')
    env = dict(env0, SPORE_LANE='4', SPORE_HEADLESS_NAME='sporecoop-headless-trial')
    got = subprocess.run(['bash', sh, 'name'], env=env, capture_output=True, text=True).stdout.strip()
    check(got == 'sporecoop-headless-trial', f'SPORE_HEADLESS_NAME names the shell outright ({got})')
    check(labbudget.lane_of_dir('/x/SPORE-lab/lane7/SporebinEP1') == 7 and labbudget.lane_of_dir('/x/SPORE/SporebinEP1') == 1,
          'a game\'s lane from its working directory (lane 1: the main install)')


CFG = dict(games=18, vram_game=0.66, vram_free=0.5, display_games=10, xw_admit=0.8, ls_games=12)


def snap(games=0, per_display=None, per_lane=None, vram=None, size=16.0, xw=None):
    return dict(games=games, per_display=per_display or {}, per_lane=per_lane or {}, vram_used=vram, vram_total=size,
                xw=xw or {})


def case_budget():
    print('D1: the machine budget')
    A, B = 'sporecoop-headless', 'sporecoop-headless-2'
    d = labbudget.decide
    check(d(2, 4, B, snap(4, {A: 4}, {2: 2, 3: 2}, 6.5), [], CFG) == '', 'a 2-game test on a calm machine starts')
    why = d(4, 4, B, snap(16, {A: 8, B: 8}, {2: 4, 3: 4, 5: 4, 6: 4}, 11.0), [], CFG)
    check('budget of 18' in why, f'16 games run: a 4-game test waits for the game budget ({why[:60]})')
    check(d(4, 4, B, snap(16, {A: 8, B: 8}, {4: 4, 3: 4, 5: 4, 6: 4}, 11.0), [], CFG) == '',
          'the same with its own lane already running 4 games (warm): nothing added, it starts')
    why = d(2, 4, B, snap(10, {A: 6, B: 4}, {2: 2}, 15.0), [], CFG)
    check('VRAM' in why, f'VRAM 15 of 16 GB used: waits ({why[:60]})')
    check(d(2, 4, B, snap(10, {A: 6, B: 4}, {2: 2}, None, None), [], CFG) == '', 'no VRAM reading: the VRAM rule is out')
    why = d(2, 4, B, snap(12, {A: 2, B: 10}, {}, 8.0), [], CFG)
    check('display' in why and B in why, f'10 games on its display already: waits ({why[:70]})')
    check(d(2, 5, A, snap(12, {A: 2, B: 10}, {}, 8.0), [], CFG) == '', 'a lane of the other display starts then')
    why = d(2, 4, B, snap(4, {B: 4}, {}, 6.0, xw={B: 0.93}), [], CFG)
    check('Xwayland' in why, f'its display\'s Xwayland at 93 %: waits ({why[:60]})')
    pend = [(2, A, 4, 2.6), (3, B, 4, 2.6)]
    why = d(4, 4, B, snap(10, {A: 5, B: 5}, {}, 9.0), pend, CFG)
    check('starting' in why, f'10 games run and 8 more starting: a 4-game test waits ({why[:70]})')
    why = d(2, 4, B, snap(12, {A: 6, B: 6}, {}, 9.0), [], CFG, sensitive=True)
    check('fps' in why, f'a load-sensitive test with 12 games running waits for a calmer machine ({why[:60]})')
    why = d(4, 4, B, snap(10, {A: 5, B: 5}, {}, 9.0), [], CFG, ls_running=True)
    check('load-sensitive' in why, f'while one runs, a 4-game test that would make 14 waits ({why[:60]})')
    check(d(0, 4, B, snap(30, {}, {}, 15.9), [], CFG) == '', 'a test without games (make test) is never held')

    # the gate on fake readings
    state = {'s': snap(16, {A: 8, B: 8}, {2: 4, 3: 4, 5: 4, 6: 4}, 11.0)}
    said = []
    g = labbudget.Gate(said.append, games_of=lambda t: t['games'], sensitive_of=lambda t: False, snap=lambda: state['s'])
    old = (labbudget.POLL, labbudget.WAIT)
    labbudget.POLL, labbudget.WAIT = 0.05, 0.6
    try:
        got = []
        th = threading.Thread(target=lambda: got.append(g.admit(dict(name='camp', games=4), 4, poll=0.05)), daemon=True)
        th.start()
        time.sleep(0.2)
        check(not got and any('camp on lane 4 waits' in x for x in said), f'the gate holds a 4-game test ({said[:1]})')
        state['s'] = snap(12, {A: 8, B: 4}, {2: 4, 3: 4, 5: 4}, 9.0)
        th.join(3)
        check(got and isinstance(got[0], list), 'and starts it when a campaign of the machine ended')
        tok = got[0] if got else None
        got2 = []
        th = threading.Thread(target=lambda: got2.append(g.admit(dict(name='next', games=4), 6, poll=0.05)), daemon=True)
        th.start()
        time.sleep(0.3)
        check(not got2, 'right after it a 4-game test on lane 6 waits: the started test\'s 4 games count as starting '
              '(12 + 4 + 4 > 18) though its lane runs none yet')
        g.release(tok)
        th.join(3)
        check(got2 and isinstance(got2[0], list), 'it starts once the first test ended')
        if got2 and isinstance(got2[0], list):
            g.release(got2[0])
        g.held.clear()
        state['s'] = snap(18, {A: 9, B: 9}, {}, 12.0)
        t0 = time.time()
        r = g.admit(dict(name='late', games=2), 4, poll=0.05)
        check(isinstance(r, str) and 'not started' in r and time.time() - t0 < 5,
              f'nothing of the run running and no room: not started after the wait ({str(r)[:70]})')
        ev = threading.Event()
        ev.set()
        check(g.admit(dict(name='int', games=2), 4, ev, poll=0.05) is None, 'an interrupt ends the wait')
        os.environ['SPORE_LAB_BUDGET'] = '0'
        check(g.admit(dict(name='off', games=4), 4) == [], 'SPORE_LAB_BUDGET=0: no budget')
    finally:
        os.environ.pop('SPORE_LAB_BUDGET', None)
        labbudget.POLL, labbudget.WAIT = old


def case_fpslimit():
    print('D1: fps-scaled limits')
    sys.argv = [sys.argv[0]]
    lim = lambda f: fpslimit.scaled(f, 0.10, 3.0, 0.20)  # noqa: E731 (coop_npc_walk's numbers)
    check(lim(66) == 0.10 and lim(30) == 0.10, 'npc_walk_tribe p50: 0.10 m at 30 fps and faster (unchanged)')
    check(0.11 < lim(22) < 0.14, f'0.136 m at 22 fps: the 0.7.14 final check\'s 0.11 m (fix on) passes ({lim(22):.3f})')
    check(0.17 < lim(16) < 0.19, f'0.19 m at 16 fps: rel/0.7.14 20261114-050940\'s 0.17 m passes ({lim(16):.3f})')
    check(lim(5) == 0.20 and lim(0) == 0.10, 'at most 0.20 m (the twin FAILs measured 0.27-0.29 m); no fps: 0.10')
    check(all(lim(f) < 0.27 for f in range(1, 241)), 'no fps lets a recorded twin value (0.27 m) pass')


CASES = [case_until, case_settle, case_menu, case_displays, case_budget, case_fpslimit]


def main():
    os.environ.pop('SPORE_START_FIXED', None)
    for f in CASES:
        f()
    print('RESULT:', 'OK' if not FAILS else f'FAIL ({len(FAILS)})')
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
