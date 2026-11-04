#!/usr/bin/env python3
"""BUG-0347: the watchdog tells a paused game (minimized, hidden: no frames, the game loop runs)
from a real hang (no frames, no loop), one game of the lane.

Players' logs held a `WATCHDOG (no frames ...)` stack dump after every alt-tab: the window was
minimized, App::Canvas::Tick slept 200 ms per pass, SporeKit saw no frame for 30 s and dumped the
main thread's stack as if the game hung.

  minimize  `kit.test.window minimize` for 40 s: the frames stop, the loop goes on: the log has the
            INFO line "... the game loop runs ... not a hang" and no WATCHDOG 'no frames' dump
  hide      (only with --case) the window hidden: under Wine the game goes on presenting (without
            the frame cap, ~80 000 frames/s), so there is no stall to tell apart; not in the default
  hang      `kit.test.hang 40000` blocks the main thread: a real hang still writes the dump and
            the hang report

  kit_watchdog.py [--old] [--case minimize,hang]
  (--old or SPORE_FIX_OFF=hangloop: the switch off, as 0.7.4: minimize / hide FAIL, hang PASSES)"""
import os
import re
import socket
import sys
import time

import lane
from twoinst import Checks, wait_up
from sporectl import Console  # (after twoinst: it puts tools/ on the path)

GAME = lane.game_dir()
LOGDIR = os.path.join(GAME, 'SporebinEP1', 'mods', 'logs')
CASES = ['minimize', 'hang']


def logname():
    return os.path.join(LOGDIR, f'sporekit.{lane.HOST}.log' if lane.HOST > 1 else 'sporekit.log')


def log_size():
    try:
        return os.path.getsize(logname())
    except OSError:
        return 0


def log_since(off):
    with open(logname(), 'rb') as f:
        f.seek(off)
        return f.read().decode('utf-8', 'replace')


def timed(line, timeout=10):
    c = Console(lane.HOST, timeout=timeout)
    try:
        return c.call(line)
    except (OSError, ValueError, socket.timeout):
        return None


def frame():
    r = timed('frame')
    m = re.search(r'frame=(\d+)', r['out']) if r else None
    return int(m.group(1)) if m else -1


def start_host(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'the game starts')
    c = Console(lane.HOST)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 60 60000')
    return c


def paused_case(ck, case):
    off = log_size()
    r = timed(f'kit.test.window {"minimize" if case == "minimize" else "hide"}')
    print(f'  {r["out"].strip() if r else "no answer"}')
    f0 = frame()
    time.sleep(4)
    f1 = frame()
    print(f'  frames while {case}d: {f0} -> {f1}')
    ck(f1 >= 0 and f1 - f0 < 5, f'{case}: SporeKit sees no frames ({f0} -> {f1})')
    time.sleep(38)
    log = log_since(off)
    dumps = re.findall(r'WATCHDOG \(no frames[^\n]*', log)
    paused = re.findall(r'watchdog: no frames for \d+ s, the game loop runs[^\n]*', log)
    print(f'  log: {len(dumps)} WATCHDOG no-frames dumps, pause lines: {paused[:1]}')
    ck(not dumps, f'{case}: no WATCHDOG "no frames" dump for a paused game ({dumps[:1]})')
    ck(bool(paused), f'{case}: one INFO line says the game loop runs, not a hang')
    ck('hang report:' not in log, f'{case}: no hang report')
    timed(f'kit.test.window {"restore" if case == "minimize" else "show"}')
    time.sleep(3)
    timed('kit.test.window restore')
    a = frame()
    time.sleep(2)
    b = frame()
    ck(b > a, f'{case}: frames again after the window is back ({a} -> {b})')


def hang_case(ck):
    off = log_size()
    r = timed('kit.test.hang 40000')
    print(f'  {r["out"].strip() if r else "no answer"}')
    time.sleep(44)
    log = log_since(off)
    dumps = re.findall(r'WATCHDOG \(no frames[^\n]*', log)
    print(f'  log: {dumps[:1]}; hang report: {"hang report:" in log}')
    ck(bool(dumps), 'hang: a real hang still writes the WATCHDOG "no frames" dump')
    ck('not a hang' not in log, 'hang: not taken for a pause')
    ck('hang report:' in log, 'hang: the hang report is written')
    time.sleep(3)
    a = frame()
    time.sleep(2)
    b = frame()
    ck(b > a, f'hang: frames again after the main thread lets go ({a} -> {b})')


def main():
    ck = Checks()
    old = '--old' in sys.argv or 'hangloop' in os.environ.get('SPORE_FIX_OFF', '').split(',')
    cases = CASES
    if '--case' in sys.argv:
        cases = sys.argv[sys.argv.index('--case') + 1].split(',')
    start_host(ck)
    fx = timed(f'kit.fix hangloop {0 if old else 1}')
    ck(fx is not None and fx['out'].strip().endswith(str(0 if old else 1)), 'kit.fix hangloop')
    if old:
        print('  hangloop off (the old watchdog)', flush=True)
    timed('bg on')
    res = {}
    for case in cases:
        print(f'== {case}', flush=True)
        before = ck.ok
        ck.ok = True
        if case == 'hang':
            hang_case(ck)
        else:
            paused_case(ck, case)
        res[case] = ck.ok
        ck.ok &= before
    print('cases:', ', '.join(f'{k}={"PASS" if v else "FAIL"}' for k, v in res.items()))
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
