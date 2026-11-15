"""Condition waits of the games' start-up (plan 0.7.15 A9, BUG-1112; docs/06-testing.md#testing-start-waits).

The cold start and the warm reset of twoinst (and the extra guests of coop_multi) used fixed sleeps between their
steps: 2 s after the kill, 2 s between the two launches, 4 s after the host is in its world, 4 s (cold) / 3 s (warm)
after the guest is in, 1-2 s poll steps. Each wait here polls a real signal instead (the lane's processes gone, the
guest's game process up, the game's frames advancing and its game time running), with the old value as the upper
bound: a signal that never comes costs exactly the old sleep, never more.

SPORE_START_FIXED=1 (the twin of BUG-1112): every wait sleeps its old fixed time (the start-up before 0.7.15).
Pure logic (no game, no lane): tools/tests/labstart_selftest.py tests it with fake consoles.
"""
import os
import re
import time

STEP = 0.25


def fixed():
    """SPORE_START_FIXED=1: the old fixed sleeps"""
    return os.environ.get('SPORE_START_FIXED') == '1'


def until(cond, timeout, step=STEP, clock=time.monotonic, sleep=time.sleep):
    """poll cond() every `step` s until it is true: the seconds waited, or None when `timeout` s passed first.
    A probe that raises OSError / ValueError (a console that does not answer yet) counts as not yet. Under
    SPORE_START_FIXED=1 it sleeps `timeout` s and returns None (the old fixed sleep)."""
    t0 = clock()
    if fixed():
        sleep(timeout)
        return None
    while True:
        try:
            if cond():
                return clock() - t0
        except (OSError, ValueError):
            pass
        left = timeout - (clock() - t0)
        if left <= 0:
            return None
        sleep(min(step, left))


def frame_of(con):
    """the game's frame counter (`frame`), -1 when it does not answer"""
    try:
        m = re.search(r'frame=(\d+)', con.call('frame').get('out') or '')
    except (OSError, ValueError):
        return -1
    return int(m.group(1)) if m else -1


def time_runs(con):
    """the game's time runs (`coop.pauses`: paused=0; twoinst.game_runs polls the same)"""
    try:
        return bool(re.search(r'paused=0\b', con.call('coop.pauses').get('out') or ''))
    except (OSError, ValueError):
        return False


def settle(cons, timeout, step=STEP, clock=time.monotonic, sleep=time.sleep):
    """the settle after a start step (was a fixed sleep of `timeout` s): every game in `cons` draws frames (its
    counter moved since the first look) and its game time runs. Seconds waited, None after `timeout` (the old
    sleep's length). A game standing on a stage entry's pause (twoinst.game_runs: 0-40 s) ends the wait at the
    bound, as before; the tests that need the time to run wait for it themselves."""
    first = {}

    def ok():
        good = True
        for i, c in enumerate(cons):
            f = frame_of(c)
            if i not in first:
                first[i] = f
                good = False  # the counter must move after this first look
            elif f < 0 or f <= first[i] or not time_runs(c):
                good = False
        return good
    return until(ok, timeout, step, clock, sleep)


class MenuFrames:
    """the frames a game has drawn in its galaxy menu, counted from the first poll that saw the menu (game.mode
    GalaxyGameEntry). `wait.frames 200` right before the guest's join waited 200 frames after a menu that had
    often been up for 10 s already; `wait_menu(con, 200)` waits only for what is left of those 200 frames."""
    def __init__(self):
        self.at = {}

    def note(self, key, con):
        """called by a poll that saw `con` in its menu: remembers the frame counter of the first such poll"""
        if key not in self.at:
            self.at[key] = frame_of(con)  # -1 (no answer): seen, but its frames count from the join wait

    def left(self, key, con, frames):
        """frames still to wait so that the game drew `frames` in its menu"""
        if fixed() or self.at.get(key, -1) < 0:
            return frames
        now = frame_of(con)
        return frames if now < 0 else max(0, frames - (now - self.at[key]))

    def wait_menu(self, key, con, frames, timeout_ms=60000):
        n = self.left(key, con, frames)
        if n > 0:
            con.call(f'wait.frames {n} {timeout_ms}')
        return n
