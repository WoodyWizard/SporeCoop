#!/usr/bin/env python3
"""Self-test of the lab's start-up waits and budget (plan 0.7.15 A9, A16, D1; BUG-1112..1114), no game needed
(make test). Fake consoles, a fake clock and fake /proc / sysfs readings.

  startwait   until(): true at once, true later, never (the old bound, not more), a probe that raises; settle():
              frames that move and game time that runs end it early, a game standing on its pause or a frozen
              counter keep it to the bound; MenuFrames: 200 frames counted from the first poll that saw the menu;
              SPORE_START_FIXED=1 sleeps the old fixed time
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE)))
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


CASES = [case_until, case_settle, case_menu]


def main():
    os.environ.pop('SPORE_START_FIXED', None)
    for f in CASES:
        f()
    print('RESULT:', 'OK' if not FAILS else f'FAIL ({len(FAILS)})')
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
